use thiserror::Error;

/// Errors raised while parsing a Callgrind `.out` file.
#[derive(Debug, Error)]
pub enum ParseError {
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
    #[error("bad id: {0}")]
    BadId(#[from] std::num::ParseIntError),
    #[error("call record missing required cfn=")]
    MissingCfn,
    #[error("unexpected end of input")]
    UnexpectedEof,
}

/// Errors raised while serializing a `CallGraph` to JSON.
#[derive(Debug, Error)]
pub enum ToJsonError {
    #[error("serde error: {0}")]
    Serde(#[from] serde_json::Error),
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
}

/// Errors raised while rendering a `CallGraph` to a flamegraph SVG.
#[derive(Debug, Error)]
pub enum FlamegraphError {
    #[error("the graph carries no cost data (all self/inclusive costs are zero)")]
    NoCost,
    #[error("inferno flamegraph error: {0}")]
    Inferno(String),
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
}
