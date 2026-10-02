//! A call's params read against the node's JSON schema: checked, defaults
//! filled in, then handed to serde as the node's own type.

use std::fmt::Write as _;

use serde::de::DeserializeOwned;
use serde::Deserialize;
use serde_json::{Map, Value};

/// The schema of a node that takes no params.
pub const NO_PARAMS: &str = r#"{"type":"object","properties":{},"additionalProperties":false}"#;

/// The params of a node that takes none.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NoParams {}

/// `params` read as a `P` against `schema`: an empty string is `{}`, a param
/// set to null is a param not set, every param is checked against the
/// schema, the schema's defaults fill in what is not set, and a whole number
/// a param declared `integer` is given as `30.0` reads as `30`. Answers the
/// params and the object they were read from.
///
/// The schema keywords checked are `type`, `enum`, `const`, `minimum`,
/// `maximum`, `exclusiveMinimum`, `exclusiveMaximum`, `minLength`,
/// `maxLength`, `minItems`, `maxItems`, `items`, `properties`, `required`
/// and `additionalProperties`; the compiler checks the call's params against
/// the whole schema before a node sees them.
pub fn read<P: DeserializeOwned>(schema: &str, params: &str) -> Result<(P, Value), String> {
    let schema: Value = serde_json::from_str(schema)
        .map_err(|err| format!("the params schema is not JSON: {err}"))?;
    let text = params.trim();
    let mut value: Value = if text.is_empty() {
        Value::Object(Map::new())
    } else {
        serde_json::from_str(text).map_err(|err| format!("the params are not JSON: {err}"))?
    };
    let Value::Object(object) = &mut value else {
        return Err(format!("the params are a JSON object, not {text}"));
    };
    object.retain(|_, value| !value.is_null());
    check(&mut value, &schema, "")?;
    fill_defaults(&mut value, &schema);
    let params = serde_json::from_value(value.clone())
        .map_err(|err| format!("the params do not read: {err}"))?;
    Ok((params, value))
}

fn fill_defaults(value: &mut Value, schema: &Value) {
    let (Value::Object(object), Some(Value::Object(properties))) =
        (value, schema.get("properties"))
    else {
        return;
    };
    for (name, property) in properties {
        if let Some(default) = property.get("default") {
            object
                .entry(name.clone())
                .or_insert_with(|| default.clone());
        }
    }
}

fn named(path: &str) -> String {
    if path.is_empty() {
        "the params".to_owned()
    } else {
        format!("`{path}`")
    }
}

fn type_matches(value: &Value, kind: &str) -> bool {
    match kind {
        "null" => value.is_null(),
        "boolean" => value.is_boolean(),
        "string" => value.is_string(),
        "array" => value.is_array(),
        "object" => value.is_object(),
        "number" => value.is_number(),
        "integer" => match value {
            Value::Number(number) => {
                number.is_i64()
                    || number.is_u64()
                    || number.as_f64().is_some_and(|float| float.fract() == 0.0)
            }
            _ => false,
        },
        _ => true,
    }
}

fn check(value: &mut Value, schema: &Value, path: &str) -> Result<(), String> {
    let Value::Object(schema) = schema else {
        return Ok(());
    };
    if let Some(kind) = schema.get("type") {
        let kinds: Vec<&str> = match kind {
            Value::String(kind) => vec![kind.as_str()],
            Value::Array(kinds) => kinds.iter().filter_map(Value::as_str).collect(),
            _ => Vec::new(),
        };
        if !kinds.is_empty() && !kinds.iter().any(|kind| type_matches(value, kind)) {
            return Err(format!(
                "{} is {}, not {value}",
                named(path),
                kinds.join(" or ")
            ));
        }
        if kinds.contains(&"integer") && !kinds.contains(&"number") {
            if let Some(whole) = value.as_f64().filter(|float| float.fract() == 0.0) {
                if !value.is_i64() && !value.is_u64() {
                    *value = Value::from(whole as i64);
                }
            }
        }
    }
    if let Some(Value::Array(allowed)) = schema.get("enum") {
        if !allowed.contains(value) {
            let mut listed = String::new();
            for (n, option) in allowed.iter().enumerate() {
                let _ = write!(listed, "{}{option}", if n == 0 { "" } else { ", " });
            }
            return Err(format!("{} is one of {listed}, not {value}", named(path)));
        }
    }
    if let Some(constant) = schema.get("const") {
        if constant != value {
            return Err(format!("{} is {constant}, not {value}", named(path)));
        }
    }
    if let Some(number) = value.as_f64() {
        let bound = |key: &str| schema.get(key).and_then(Value::as_f64);
        if let Some(min) = bound("minimum").filter(|min| number < *min) {
            return Err(format!("{} is at least {min}, not {value}", named(path)));
        }
        if let Some(max) = bound("maximum").filter(|max| number > *max) {
            return Err(format!("{} is at most {max}, not {value}", named(path)));
        }
        if let Some(min) = bound("exclusiveMinimum").filter(|min| number <= *min) {
            return Err(format!("{} is more than {min}, not {value}", named(path)));
        }
        if let Some(max) = bound("exclusiveMaximum").filter(|max| number >= *max) {
            return Err(format!("{} is less than {max}, not {value}", named(path)));
        }
    }
    let count = |key: &str| schema.get(key).and_then(Value::as_u64);
    if let Value::String(text) = value {
        let chars = text.chars().count() as u64;
        if let Some(min) = count("minLength").filter(|min| chars < *min) {
            return Err(format!(
                "{} is at least {}",
                named(path),
                counted(min, "character")
            ));
        }
        if let Some(max) = count("maxLength").filter(|max| chars > *max) {
            return Err(format!(
                "{} is at most {}",
                named(path),
                counted(max, "character")
            ));
        }
    }
    if let Value::Array(items) = value {
        let len = items.len() as u64;
        if let Some(min) = count("minItems").filter(|min| len < *min) {
            return Err(format!(
                "{} holds at least {}",
                named(path),
                counted(min, "item")
            ));
        }
        if let Some(max) = count("maxItems").filter(|max| len > *max) {
            return Err(format!(
                "{} holds at most {}",
                named(path),
                counted(max, "item")
            ));
        }
        if let Some(item_schema) = schema.get("items") {
            for (n, item) in items.iter_mut().enumerate() {
                check(item, item_schema, &format!("{}[{n}]", path_or(path)))?;
            }
        }
    }
    if let Value::Object(object) = value {
        let properties = schema.get("properties").and_then(Value::as_object);
        if let Some(Value::Array(required)) = schema.get("required") {
            for name in required.iter().filter_map(Value::as_str) {
                if !object.contains_key(name) {
                    return Err(format!("{} is required", named(&join(path, name))));
                }
            }
        }
        let closed = schema.get("additionalProperties") == Some(&Value::Bool(false));
        for (name, field) in object.iter_mut() {
            match properties.and_then(|properties| properties.get(name)) {
                Some(property) => check(field, property, &join(path, name))?,
                None if closed => {
                    let known: Vec<String> = properties
                        .map(|properties| properties.keys().map(|key| format!("`{key}`")).collect())
                        .unwrap_or_default();
                    let takes = if known.is_empty() {
                        "takes none".to_owned()
                    } else {
                        format!("takes {}", known.join(", "))
                    };
                    return Err(format!(
                        "{} is not a param here; {}",
                        named(&join(path, name)),
                        if path.is_empty() {
                            format!("the node {takes}")
                        } else {
                            format!("{} {takes}", named(path))
                        }
                    ));
                }
                None => {}
            }
        }
    }
    Ok(())
}

fn counted(n: u64, what: &str) -> String {
    if n == 1 {
        format!("1 {what}")
    } else {
        format!("{n} {what}s")
    }
}

fn path_or(path: &str) -> &str {
    if path.is_empty() {
        "params"
    } else {
        path
    }
}

fn join(path: &str, name: &str) -> String {
    if path.is_empty() {
        name.to_owned()
    } else {
        format!("{path}.{name}")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const SCHEMA: &str = r#"{
        "type": "object",
        "properties": {
            "every": {"type": "integer", "minimum": 1, "default": 30},
            "amount": {"type": "number", "minimum": 0, "maximum": 1, "default": 0.5},
            "text": {"type": "string", "minLength": 1},
            "mode": {"enum": ["over", "under"], "default": "over"},
            "fps": {"type": ["number", "null"]}
        },
        "required": ["text"],
        "additionalProperties": false
    }"#;

    #[derive(Debug, Deserialize, PartialEq)]
    struct Params {
        every: u32,
        amount: f64,
        text: String,
        mode: String,
        fps: Option<f64>,
    }

    #[test]
    fn defaults_fill_what_the_call_left_out() {
        let (params, value) = read::<Params>(SCHEMA, r#"{"text":"hi"}"#).unwrap();
        assert_eq!(
            params,
            Params {
                every: 30,
                amount: 0.5,
                text: "hi".to_owned(),
                mode: "over".to_owned(),
                fps: None
            }
        );
        assert_eq!(value["every"], 30);
    }

    #[test]
    fn a_whole_float_reads_as_an_integer() {
        let (params, _) = read::<Params>(SCHEMA, r#"{"text":"hi","every":15.0}"#).unwrap();
        assert_eq!(params.every, 15);
        let err = read::<Params>(SCHEMA, r#"{"text":"hi","every":1.5}"#).unwrap_err();
        assert!(err.contains("`every` is integer"), "{err}");
    }

    #[test]
    fn null_is_not_set() {
        let (params, _) =
            read::<Params>(SCHEMA, r#"{"text":"hi","fps":null,"every":null}"#).unwrap();
        assert_eq!((params.fps, params.every), (None, 30));
    }

    #[test]
    fn refusals_name_the_param() {
        let cases = [
            (r#"{"text":"hi","every":0}"#, "`every` is at least 1"),
            (r#"{"text":"hi","amount":2}"#, "`amount` is at most 1"),
            (r#"{"text":""}"#, "`text` is at least 1 character"),
            (r#"{"text":"hi","mode":"sideways"}"#, "`mode` is one of"),
            (
                r#"{"text":"hi","colour":"red"}"#,
                "`colour` is not a param here",
            ),
            (r#"{}"#, "`text` is required"),
            (r#"[1]"#, "a JSON object"),
            (r#"{"text":7}"#, "`text` is string"),
        ];
        for (params, said) in cases {
            let err = read::<Params>(SCHEMA, params).unwrap_err();
            assert!(err.contains(said), "{params}: {err}");
        }
    }

    #[test]
    fn no_params_reads_nothing() {
        assert_eq!(read::<NoParams>(NO_PARAMS, "").unwrap().0, NoParams {});
        assert_eq!(read::<NoParams>(NO_PARAMS, " {} ").unwrap().0, NoParams {});
        assert!(read::<NoParams>(NO_PARAMS, r#"{"x":1}"#).is_err());
    }
}
