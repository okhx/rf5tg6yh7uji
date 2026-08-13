use std::io::{self, Write};

const BLOB: &[u8] = include_bytes!(env!("MINDGUARD_TEST_BLOB"));
const SHARE: &[u8; 256] = include_bytes!(env!("MINDGUARD_TEST_SHARE"));

fn main() {
    let written = mindguard_static_core::with_decoded_packed(
        BLOB,
        SHARE,
        0x0123_4567_89ab_cdef,
        2,
        |plaintext| io::stdout().write_all(plaintext),
    ).unwrap_or_else(|_| std::process::exit(1));
    written.unwrap_or_else(|_| std::process::exit(2));
}
