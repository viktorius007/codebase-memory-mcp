fn main() {
    let cbm = std::env::var("CBM_INTERNAL")
        .unwrap_or(format!("{}/../../../internal/cbm", env!("CARGO_MANIFEST_DIR")));
    let g = std::env::var("RUST_GRAMMAR_DIR").unwrap_or(format!("{cbm}/vendored/grammars/rust"));
    println!("cargo:rerun-if-env-changed=RUST_GRAMMAR_DIR");
    println!("cargo:rerun-if-changed=src/facts.c");
    println!("cargo:rerun-if-changed={g}/parser.c");
    cc::Build::new()
        .std("c11").define("_DEFAULT_SOURCE", None).opt_level(2).warnings(false)
        .include(&cbm)
        .include(format!("{cbm}/vendored/ts_runtime/include"))
        .include(format!("{cbm}/vendored/ts_runtime/src"))
        .include(&g)
        .file(format!("{cbm}/ts_runtime.c"))
        .file(format!("{g}/parser.c"))
        .file(format!("{g}/scanner.c"))
        .file("src/facts.c")
        .compile("tsfacts");
}
