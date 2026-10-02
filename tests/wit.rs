//! The crate's `wit/av.wit` against the one `FFRWD_WIT_DIR` names, when it
//! names one: the bindings are only as current as that copy.

use std::path::{Path, PathBuf};

fn text(path: &Path) -> String {
    std::fs::read_to_string(path)
        .unwrap_or_else(|err| panic!("read {}: {err}", path.display()))
        .replace("\r\n", "\n")
}

fn ours() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("wit/av.wit")
}

#[test]
fn the_wit_is_the_one_ffrwd_wit_dir_names() {
    let Some(dir) = std::env::var_os("FFRWD_WIT_DIR") else {
        return;
    };
    let named = PathBuf::from(dir).join("av.wit");
    assert!(
        text(&named) == text(&ours()),
        "{} differs from {}; copy it over and rebuild",
        ours().display(),
        named.display()
    );
}

#[test]
fn the_wit_is_the_node_world() {
    let wit = text(&ours());
    assert!(wit.lines().any(|line| line == "package ffrwd:av@0.19.1;"));
    assert!(wit.contains("world node-module {"));
}
