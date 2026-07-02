use crate::model::ParseOptions;

/// Return the last path segment after the final `/`.
///
/// `"foo/bar/baz.c"` -> `"baz.c"`; `"baz.c"` -> `"baz.c"`; `""` -> `""`.
pub(crate) fn basename(path: &str) -> &str {
    match path.rfind('/') {
        Some(i) => &path[i + 1..],
        None => path,
    }
}

/// Normalize a file/object path according to `opts`.
///
/// When `normalize_paths` is disabled the path is returned verbatim.
/// Otherwise the basename is taken and Callgrind-style unknowns (empty or
/// `"???"`) collapse to `opts.unknown`.
pub(crate) fn normalize_path(path: &str, opts: &ParseOptions) -> String {
    if !opts.normalize_paths {
        return path.to_string();
    }
    let leaf = basename(path);
    if leaf.is_empty() || leaf == "???" {
        opts.unknown.clone()
    } else {
        leaf.to_string()
    }
}
