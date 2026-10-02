use serde::Serialize;

use crate::rows::Cue;
use crate::shape::{Kind, Shape};
use crate::types::{Frame, Packet};
use crate::Rational;

/// One thing leaving on a port. Its pts and duration are in the port's time
/// base.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Payload {
    /// New bytes in the port's format.
    Frame {
        pts: i64,
        duration: Option<i64>,
        data: Vec<u8>,
    },
    /// An input frame's bytes leaving uncopied: frame `index` of stream `id`
    /// this tick.
    Same {
        pts: i64,
        duration: Option<i64>,
        id: u32,
        index: u32,
    },
    /// One message; empty is a progress mark, never delivered.
    Message {
        pts: i64,
        data: Vec<u8>,
    },
    Packet(Packet),
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Emission {
    pub port: String,
    pub payload: Payload,
}

/// What one tick produced.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Emitted {
    pub items: Vec<Emission>,
    /// Rows for the run's rows output.
    pub reports: Vec<String>,
    pub finished: bool,
}

impl Emitted {
    /// The messages that left on `port`, as `(pts, text)`.
    pub fn messages(&self, port: &str) -> Vec<(i64, String)> {
        self.items
            .iter()
            .filter(|item| item.port == port)
            .filter_map(|item| match &item.payload {
                Payload::Message { pts, data } => {
                    Some((*pts, String::from_utf8_lossy(data).into_owned()))
                }
                _ => None,
            })
            .collect()
    }

    /// The payloads that left on `port`, in order.
    pub fn on(&self, port: &str) -> Vec<&Payload> {
        self.items
            .iter()
            .filter(|item| item.port == port)
            .map(|item| &item.payload)
            .collect()
    }
}

struct Port {
    name: String,
    kind: Kind,
    time_base: Option<Rational>,
    last_pts: Option<i64>,
    last_dts: Option<i64>,
}

/// Where a tick's emissions go. Each one is checked as it is made: the port
/// is one the shape declares and of the payload's kind, and its pts never go
/// back on that port, within a call or across calls, nor a packet's dts.
pub struct Out {
    ports: Vec<Port>,
    clock: Rational,
    emitted: Emitted,
}

impl Out {
    pub(crate) fn new(shape: &Shape) -> Out {
        Out {
            ports: shape
                .outputs
                .iter()
                .map(|output| Port {
                    name: output.name.clone(),
                    kind: output.kind,
                    time_base: output.time_base,
                    last_pts: None,
                    last_dts: None,
                })
                .collect(),
            clock: Rational::MICROS,
            emitted: Emitted::default(),
        }
    }

    pub(crate) fn begin(&mut self, clock: Rational) {
        self.clock = clock;
    }

    pub(crate) fn take(&mut self) -> Emitted {
        std::mem::take(&mut self.emitted)
    }

    fn port(&mut self, name: &str, kinds: &[Kind]) -> Result<&mut Port, String> {
        let Some(port) = self.ports.iter_mut().find(|port| port.name == name) else {
            return Err(format!("`{name}` is not an output of this node"));
        };
        if !kinds.contains(&port.kind) {
            return Err(format!(
                "`{name}` is a {} output",
                format!("{:?}", port.kind).to_lowercase()
            ));
        }
        Ok(port)
    }

    fn stamp(&mut self, name: &str, kinds: &[Kind], pts: i64) -> Result<(), String> {
        let port = self.port(name, kinds)?;
        if let Some(last) = port.last_pts.filter(|last| pts < *last) {
            return Err(format!(
                "`{name}` would go back from pts {last} to {pts}; a port's pts never decrease"
            ));
        }
        port.last_pts = Some(pts);
        Ok(())
    }

    fn push(&mut self, port: &str, payload: Payload) {
        self.emitted.items.push(Emission {
            port: port.to_owned(),
            payload,
        });
    }

    /// The time base `port` is counted in: its own, or the clock's.
    pub fn time_base(&self, port: &str) -> Result<Rational, String> {
        self.ports
            .iter()
            .find(|candidate| candidate.name == port)
            .map(|found| found.time_base.unwrap_or(self.clock))
            .ok_or_else(|| format!("`{port}` is not an output of this node"))
    }

    /// `seconds` as a pts on `port`.
    pub fn pts(&self, port: &str, seconds: f64) -> Result<i64, String> {
        Ok(self.time_base(port)?.pts(seconds))
    }

    /// The last pts that left on `port`, which the next may not be under.
    pub fn last(&self, port: &str) -> Option<i64> {
        self.ports
            .iter()
            .find(|candidate| candidate.name == port)
            .and_then(|found| found.last_pts)
    }

    /// New bytes on a video or audio port: one picture tightly packed, or a
    /// run of interleaved samples.
    pub fn frame(
        &mut self,
        port: &str,
        pts: i64,
        duration: Option<i64>,
        data: Vec<u8>,
    ) -> Result<(), String> {
        self.stamp(port, &[Kind::Video, Kind::Audio], pts)?;
        self.push(
            port,
            Payload::Frame {
                pts,
                duration,
                data,
            },
        );
        Ok(())
    }

    /// Frame `index` of stream `id` leaving on `port` uncopied, at `pts`.
    /// Its format has to be the port's.
    pub fn same(
        &mut self,
        port: &str,
        pts: i64,
        duration: Option<i64>,
        id: u32,
        index: u32,
    ) -> Result<(), String> {
        self.stamp(port, &[Kind::Video, Kind::Audio], pts)?;
        self.push(
            port,
            Payload::Same {
                pts,
                duration,
                id,
                index,
            },
        );
        Ok(())
    }

    /// `frame` of stream `id` leaving on `port` unchanged at its own pts and
    /// duration: a filter passing a picture through, on a port in the
    /// stream's time base.
    pub fn pass(&mut self, port: &str, id: u32, frame: &Frame) -> Result<(), String> {
        self.same(port, frame.pts, frame.duration, id, frame.index)
    }

    /// One message on a data port.
    pub fn message(&mut self, port: &str, pts: i64, data: Vec<u8>) -> Result<(), String> {
        self.stamp(port, &[Kind::Data], pts)?;
        self.push(port, Payload::Message { pts, data });
        Ok(())
    }

    /// `row` as one JSON message on `port` at `pts`.
    pub fn row<T: Serialize>(&mut self, port: &str, pts: i64, row: &T) -> Result<(), String> {
        let data = serde_json::to_vec(row).map_err(|err| format!("a row for `{port}`: {err}"))?;
        self.message(port, pts, data)
    }

    /// Each of `rows` as a message on `port` at `pts`.
    pub fn rows<T: Serialize>(&mut self, port: &str, pts: i64, rows: &[T]) -> Result<(), String> {
        rows.iter().try_for_each(|row| self.row(port, pts, row))
    }

    /// `cue` on `port`, stamped at its start.
    pub fn cue(&mut self, port: &str, cue: &Cue) -> Result<(), String> {
        let pts = self.pts(port, cue.start_t)?;
        self.row(port, pts, cue)
    }

    /// A progress mark: nothing more will leave on `port` stamped before
    /// `pts`. Never delivered.
    pub fn progress(&mut self, port: &str, pts: i64) -> Result<(), String> {
        self.message(port, pts, Vec::new())
    }

    /// One packet on a packets port, in decode order: its dts never
    /// decreases.
    pub fn packet(&mut self, port: &str, packet: Packet) -> Result<(), String> {
        let found = self.port(port, &[Kind::Packets])?;
        if let Some(dts) = packet.dts {
            if let Some(last) = found.last_dts.filter(|last| dts < *last) {
                return Err(format!(
                    "`{port}` would go back from dts {last} to {dts}; packets keep decode order"
                ));
            }
            found.last_dts = Some(dts);
        }
        self.push(port, Payload::Packet(packet));
        Ok(())
    }

    /// One row for the run's rows output, as a sink writes them.
    pub fn report<T: Serialize>(&mut self, row: &T) -> Result<(), String> {
        let text = serde_json::to_string(row).map_err(|err| format!("a report row: {err}"))?;
        self.emitted.reports.push(text);
        Ok(())
    }

    /// Nothing more will leave: the host makes the last call and ends every
    /// output. A node on an input clock ends with it and need not say so.
    pub fn finish(&mut self) {
        self.emitted.finished = true;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::shape::{Bound, Input, Output};

    fn out() -> Out {
        let shape = Shape::new()
            .input(Input::video("v").clock())
            .input(Input::packets("coded"))
            .output(Output::like("v"))
            .output(Output::rows("spots").time_base(Rational::new(1, 1000)))
            .output(Output::packets("p").following("coded"))
            .resolve(&Bound::new(&["v", "coded"]));
        let mut out = Out::new(&shape.unwrap_or_else(|err| panic!("{err}")));
        out.begin(Rational::new(1, 15));
        out
    }

    #[test]
    fn a_port_never_goes_back() {
        let mut out = out();
        out.same("v", 2, Some(1), 0, 0).unwrap();
        out.same("v", 2, Some(1), 0, 0).unwrap();
        let err = out.same("v", 1, Some(1), 0, 0).unwrap_err();
        assert!(err.contains("from pts 2 to 1"), "{err}");
        out.take();
        assert!(out.frame("v", 1, None, vec![]).is_err());
        out.row("spots", 0, &1).unwrap();
        assert_eq!(out.last("spots"), Some(0));
    }

    #[test]
    fn ports_and_kinds_are_checked() {
        let mut out = out();
        assert!(out.row("nowhere", 0, &1).is_err());
        assert!(out.row("v", 0, &1).unwrap_err().contains("video output"));
        assert!(out.frame("spots", 0, None, vec![]).is_err());
    }

    #[test]
    fn packets_keep_decode_order() {
        let mut out = out();
        let packet = |pts, dts| Packet {
            pts,
            dts,
            ..Packet::default()
        };
        out.packet("p", packet(3, None)).unwrap();
        out.packet("p", packet(3, Some(0))).unwrap();
        out.packet("p", packet(1, Some(1))).unwrap();
        assert!(out.packet("p", packet(2, Some(0))).is_err());
    }

    #[test]
    fn seconds_land_in_the_port_time_base() {
        let mut out = out();
        assert_eq!(out.pts("v", 2.0).unwrap(), 30);
        assert_eq!(out.pts("spots", 2.0).unwrap(), 2000);
        out.cue("spots", &Cue::new(1.5, 2.0, "hi")).unwrap();
        let emitted = out.take();
        assert_eq!(
            emitted.messages("spots"),
            [(
                1500,
                r#"{"start_t":1.5,"end_t":2.0,"text":"hi"}"#.to_owned()
            )]
        );
    }
}
