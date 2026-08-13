#![forbid(unsafe_code)]

use std::hint::black_box;

const MAGIC: &[u8; 8] = b"MGSTV1\0\0";
const HEADER_SIZE: usize = 96;
const MAX_PLAINTEXT: usize = 64 * 1024;
const GOLDEN: u64 = 0x9e37_79b9_7f4a_7c15;
const MIX: u64 = 0xd6e8_feb8_6659_fd93;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DecodeError {
    Bounds,
    Magic,
    Header,
    Version,
    Profile,
    Kind,
    Site,
    Tag,
}

pub struct Plaintext(Vec<u8>);

struct Material([u8; 32]);

impl Drop for Material {
    fn drop(&mut self) {
        self.0.fill(0);
        black_box(&mut self.0);
    }
}

impl Plaintext {
    pub fn as_bytes(&self) -> &[u8] {
        &self.0
    }
}

impl Drop for Plaintext {
    fn drop(&mut self) {
        self.0.fill(0);
        black_box(&mut self.0);
    }
}

fn read_u16(bytes: &[u8], offset: usize) -> u16 {
    u16::from_le_bytes(bytes[offset..offset + 2].try_into().unwrap())
}

fn read_u32(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap())
}

fn read_u64(bytes: &[u8], offset: usize) -> u64 {
    u64::from_le_bytes(bytes[offset..offset + 8].try_into().unwrap())
}

fn words(material: &[u8; 32]) -> [u64; 4] {
    std::array::from_fn(|i| u64::from_le_bytes(material[i * 8..i * 8 + 8].try_into().unwrap()))
}

fn arx_block(material: &[u8; 32], site: u64, diversifier: u64, counter: u64) -> [u8; 32] {
    const ROTATIONS: [[u32; 4]; 4] = [
        [32, 24, 16, 63],
        [31, 17, 47, 23],
        [13, 37, 29, 43],
        [7, 19, 41, 53],
    ];
    let mut key = words(material);
    let (mut a, mut b, mut c, mut d) = (
        key[0] ^ site,
        key[1] ^ diversifier,
        key[2] ^ counter,
        key[3] ^ GOLDEN,
    );
    for round in 0..8u64 {
        let r = ROTATIONS[((diversifier ^ counter ^ round) & 3) as usize];
        a = a.wrapping_add(b);
        d = (d ^ a).rotate_left(r[0]);
        c = c.wrapping_add(d);
        b = (b ^ c).rotate_left(r[1]);
        a = a.wrapping_add(b);
        d = (d ^ a).rotate_left(r[2]);
        c = c.wrapping_add(d);
        b = (b ^ c).rotate_left(r[3]);
        a ^= GOLDEN.wrapping_add(round);
        c ^= counter.wrapping_add(round);
    }
    let mut out = [0u8; 32];
    let mut values = [a, b, c, d];
    for (chunk, value) in out.chunks_exact_mut(8).zip(&values) {
        chunk.copy_from_slice(&value.to_le_bytes());
    }
    values.fill(0);
    key.fill(0);
    a = 0; b = 0; c = 0; d = 0;
    black_box((&mut values, &mut key, &mut a, &mut b, &mut c, &mut d));
    out
}

fn crypt(data: &mut [u8], material: &[u8; 32], site: u64, diversifier: u64) {
    for (counter, chunk) in data.chunks_mut(32).enumerate() {
        let mut stream = arx_block(material, site, diversifier, counter as u64);
        for (byte, mask) in chunk.iter_mut().zip(&stream) {
            *byte ^= *mask;
        }
        stream.fill(0);
        black_box(&mut stream);
    }
}

fn compute_tag(header: &[u8], payload: &[u8], material: &[u8; 32], site: u64, diversifier: u64) -> [u8; 16] {
    let mut key = words(material);
    let (mut t0, mut t1, mut index) = (
        key[0] ^ key[2] ^ site,
        key[1] ^ key[3] ^ diversifier,
        0u64,
    );
    for byte in header[..72].iter().chain(&header[88..]).chain(payload) {
        t0 = (t0 ^ (*byte as u64).wrapping_add(index.wrapping_mul(GOLDEN)))
            .rotate_left(13)
            .wrapping_add(t1);
        t1 = t1
            .wrapping_add((*byte as u64) ^ index)
            .wrapping_add(MIX)
            .rotate_left(29)
            ^ t0;
        index = index.wrapping_add(1);
    }
    for round in 0..8u64 {
        t0 = t0
            .wrapping_add(t1)
            .wrapping_add(round.wrapping_mul(GOLDEN))
            .rotate_left(17)
            ^ key[(round & 3) as usize];
        t1 = (t1 ^ t0)
            .rotate_left(41)
            .wrapping_add(key[((round + 1) & 3) as usize]);
    }
    let mut out = [0u8; 16];
    out[..8].copy_from_slice(&t0.to_le_bytes());
    out[8..].copy_from_slice(&t1.to_le_bytes());
    key.fill(0);
    t0 = 0; t1 = 0;
    black_box((&mut key, &mut t0, &mut t1));
    out
}

fn constant_time_equal(left: &[u8], right: &[u8]) -> bool {
    if left.len() != right.len() {
        return false;
    }
    left.iter().zip(right).fold(0u8, |diff, (a, b)| diff | (a ^ b)) == 0
}

fn packed_position(logical: usize, site: u64, diversifier: u64) -> usize {
    let odd = ((site ^ diversifier.rotate_left(17)) as u8) | 1;
    let offset = (site.rotate_right(11) ^ diversifier) as u8;
    (logical as u8).wrapping_mul(odd).wrapping_add(offset) as usize
}

#[derive(Clone, Copy)]
enum Share<'a> {
    Raw(&'a [u8; 32]),
    Packed(&'a [u8; 256]),
}

fn decode_with_share(
    blob: &[u8],
    share: Share<'_>,
    expected_site: u64,
    expected_kind: u8,
) -> Result<Plaintext, DecodeError> {
    if blob.len() < HEADER_SIZE {
        return Err(DecodeError::Bounds);
    }
    if &blob[..8] != MAGIC {
        return Err(DecodeError::Magic);
    }
    if read_u16(blob, 12) as usize != HEADER_SIZE
        || read_u16(blob, 14) != 0
        || blob[88..96].iter().any(|byte| *byte != 0)
    {
        return Err(DecodeError::Header);
    }
    if read_u16(blob, 8) != 1 {
        return Err(DecodeError::Version);
    }
    if blob[10] != 1 {
        return Err(DecodeError::Profile);
    }
    if blob[11] != expected_kind || !(1..=3).contains(&blob[11]) {
        return Err(DecodeError::Kind);
    }
    if read_u64(blob, 16) != expected_site {
        return Err(DecodeError::Site);
    }
    let plaintext_len = read_u32(blob, 24) as usize;
    let payload_len = read_u32(blob, 28) as usize;
    if plaintext_len > MAX_PLAINTEXT
        || payload_len != plaintext_len
        || HEADER_SIZE.checked_add(payload_len) != Some(blob.len())
    {
        return Err(DecodeError::Bounds);
    }
    let diversifier = read_u64(blob, 32);
    let material = Material(std::array::from_fn(|i| {
        let code_byte = match share {
            Share::Raw(code_share) => code_share[i],
            Share::Packed(code_share) => (0..8).fold(0u8, |value, lane| {
                value ^ code_share[packed_position(i + lane * 32, expected_site, diversifier)]
            }),
        };
        blob[40 + i] ^ code_byte
    }));
    let mut computed = compute_tag(&blob[..HEADER_SIZE], &blob[HEADER_SIZE..], &material.0, expected_site, diversifier);
    let valid = constant_time_equal(&computed, &blob[72..88]);
    computed.fill(0);
    black_box(&mut computed);
    if !valid {
        return Err(DecodeError::Tag);
    }
    let mut plaintext = blob[HEADER_SIZE..].to_vec();
    crypt(&mut plaintext, &material.0, expected_site, diversifier);
    Ok(Plaintext(plaintext))
}

pub fn decode(
    blob: &[u8],
    code_share: &[u8; 32],
    expected_site: u64,
    expected_kind: u8,
) -> Result<Plaintext, DecodeError> {
    decode_with_share(blob, Share::Raw(code_share), expected_site, expected_kind)
}

pub fn decode_packed(
    blob: &[u8],
    code_share: &[u8; 256],
    expected_site: u64,
    expected_kind: u8,
) -> Result<Plaintext, DecodeError> {
    decode_with_share(blob, Share::Packed(code_share), expected_site, expected_kind)
}

pub fn with_decoded<R>(
    blob: &[u8],
    code_share: &[u8; 32],
    expected_site: u64,
    expected_kind: u8,
    callback: impl FnOnce(&[u8]) -> R,
) -> Result<R, DecodeError> {
    let plaintext = decode(blob, code_share, expected_site, expected_kind)?;
    Ok(callback(plaintext.as_bytes()))
}

pub fn with_decoded_packed<R>(
    blob: &[u8],
    code_share: &[u8; 256],
    expected_site: u64,
    expected_kind: u8,
    callback: impl FnOnce(&[u8]) -> R,
) -> Result<R, DecodeError> {
    let plaintext = decode_packed(blob, code_share, expected_site, expected_kind)?;
    Ok(callback(plaintext.as_bytes()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn malformed_input_fails_before_plaintext() {
        let share = [0u8; 32];
        assert!(matches!(decode(&[], &share, 1, 1), Err(DecodeError::Bounds)));
        let mut called = false;
        assert!(matches!(
            with_decoded(&[], &share, 1, 1, |_| called = true),
            Err(DecodeError::Bounds)
        ));
        assert!(!called);
        let mut blob = vec![0u8; HEADER_SIZE];
        assert!(matches!(decode(&blob, &share, 1, 1), Err(DecodeError::Magic)));
        blob[..8].copy_from_slice(MAGIC);
        blob[12..14].copy_from_slice(&(HEADER_SIZE as u16).to_le_bytes());
        assert!(matches!(decode(&blob, &share, 1, 1), Err(DecodeError::Version)));
    }
}
