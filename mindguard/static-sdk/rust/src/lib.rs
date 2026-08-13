#![forbid(unsafe_code)]

//! Explicit `dev` profile frontend for the MindGuard Static SDK bootstrap.
//!
//! Runtime expressions are rejected by the literal-only macro grammar:
//! ```compile_fail
//! use mindguard_static::mg_with_str;
//! let value = String::from("secret");
//! mg_with_str!(value.as_str(), |_| {});
//! ```
//!
//! Non-scalar literals are rejected by the sealed scalar boundary:
//! ```compile_fail
//! use mindguard_static::mg_with_value;
//! mg_with_value!("not a scalar", |_| {});
//! ```

#[cfg(not(any(feature = "dev", feature = "hardened", feature = "paranoid")))]
compile_error!("MindGuard profile is required: enable exactly one of dev, hardened, paranoid");
#[cfg(any(
    all(feature = "dev", feature = "hardened"),
    all(feature = "dev", feature = "paranoid"),
    all(feature = "hardened", feature = "paranoid")
))]
compile_error!("MindGuard profiles are mutually exclusive");
#[cfg(feature = "hardened")]
compile_error!("MindGuard Hardened requires the M2 material/blob backend; refusing an unprotected build");
#[cfg(feature = "paranoid")]
compile_error!("MindGuard Paranoid is not implemented; refusing to downgrade the profile");

pub const MAX_PLAINTEXT_BYTES: usize = 64 * 1024;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[doc(hidden)]
pub struct SiteId(u64);

#[doc(hidden)]
pub const fn make_site_id(
    target: &str,
    source: &str,
    line: u32,
    column: u32,
    ordinal: u32,
) -> SiteId {
    const OFFSET: u64 = 1_469_598_103_934_665_603;
    const PRIME: u64 = 1_099_511_628_211;
    const fn mix(mut hash: u64, bytes: &[u8]) -> u64 {
        let mut i = 0;
        while i < bytes.len() {
            hash = (hash ^ bytes[i] as u64).wrapping_mul(PRIME);
            i += 1;
        }
        hash
    }
    let mut hash = mix(OFFSET, target.as_bytes());
    hash = mix(hash, source.as_bytes());
    hash = mix(hash, &line.to_le_bytes());
    hash = mix(hash, &column.to_le_bytes());
    hash = mix(hash, &ordinal.to_le_bytes());
    SiteId(hash)
}

#[doc(hidden)]
pub trait Scalar: private::Sealed {}

mod private {
    pub trait Sealed {}
}

macro_rules! impl_scalar {
    ($($type:ty),+ $(,)?) => {
        $(
            impl private::Sealed for $type {}
            impl Scalar for $type {}
        )+
    };
}

impl_scalar!(
    bool, char, i8, i16, i32, i64, i128, isize, u8, u16, u32, u64, u128, usize, f32, f64
);

pub struct ProtectedStr {
    value: &'static str,
    _site: SiteId,
}

impl ProtectedStr {
    #[doc(hidden)]
    pub const fn new(value: &'static str, site: SiteId) -> Self {
        assert!(value.len() <= MAX_PLAINTEXT_BYTES, "MindGuard literal exceeds 64 KiB");
        Self { value, _site: site }
    }

    pub const fn as_str(&self) -> &str {
        self.value
    }
}

pub struct ProtectedBytes {
    value: &'static [u8],
    _site: SiteId,
}

impl ProtectedBytes {
    #[doc(hidden)]
    pub const fn new(value: &'static [u8], site: SiteId) -> Self {
        assert!(value.len() <= MAX_PLAINTEXT_BYTES, "MindGuard literal exceeds 64 KiB");
        Self { value, _site: site }
    }

    pub const fn as_bytes(&self) -> &[u8] {
        self.value
    }
}

pub struct ProtectedValue<T: Scalar> {
    value: T,
    _site: SiteId,
}

impl<T: Scalar> ProtectedValue<T> {
    #[doc(hidden)]
    pub const fn new(value: T, site: SiteId) -> Self {
        Self { value, _site: site }
    }

    pub fn into_inner(self) -> T {
        self.value
    }
}

#[doc(hidden)]
pub fn with_value<T: Scalar, R>(value: T, _site: SiteId, callback: impl FnOnce(T) -> R) -> R {
    callback(value)
}

#[doc(hidden)]
pub fn with_str<R>(value: &'static str, _site: SiteId, callback: impl FnOnce(&str) -> R) -> R {
    callback(value)
}

#[doc(hidden)]
pub fn with_bytes<R>(value: &'static [u8], _site: SiteId, callback: impl FnOnce(&[u8]) -> R) -> R {
    callback(value)
}

#[macro_export]
macro_rules! mg_with_str {
    ($value:literal, $callback:expr $(,)?) => {{
        const VALUE: &str = $value;
        const _: () = assert!(VALUE.len() <= $crate::MAX_PLAINTEXT_BYTES, "MindGuard literal exceeds 64 KiB");
        const SITE: $crate::SiteId = $crate::make_site_id(env!("CARGO_PKG_NAME"), file!(), line!(), column!(), 0);
        $crate::with_str(VALUE, SITE, $callback)
    }};
}

#[macro_export]
macro_rules! mg_with_bytes {
    ($value:literal, $callback:expr $(,)?) => {{
        const VALUE: &[u8] = $value;
        const _: () = assert!(VALUE.len() <= $crate::MAX_PLAINTEXT_BYTES, "MindGuard literal exceeds 64 KiB");
        const SITE: $crate::SiteId = $crate::make_site_id(env!("CARGO_PKG_NAME"), file!(), line!(), column!(), 0);
        $crate::with_bytes(VALUE, SITE, $callback)
    }};
}

#[macro_export]
macro_rules! mg_with_value {
    (-$value:literal, $callback:expr $(,)?) => {{
        const SITE: $crate::SiteId = $crate::make_site_id(env!("CARGO_PKG_NAME"), file!(), line!(), column!(), 0);
        $crate::with_value(-$value, SITE, $callback)
    }};
    ($value:literal, $callback:expr $(,)?) => {{
        const SITE: $crate::SiteId = $crate::make_site_id(env!("CARGO_PKG_NAME"), file!(), line!(), column!(), 0);
        $crate::with_value($value, SITE, $callback)
    }};
}

#[macro_export]
macro_rules! mg_str {
    ($value:literal $(,)?) => {{
        const VALUE: &str = $value;
        const _: () = assert!(VALUE.len() <= $crate::MAX_PLAINTEXT_BYTES, "MindGuard literal exceeds 64 KiB");
        const SITE: $crate::SiteId = $crate::make_site_id(env!("CARGO_PKG_NAME"), file!(), line!(), column!(), 0);
        $crate::ProtectedStr::new(VALUE, SITE)
    }};
}

#[macro_export]
macro_rules! mg_bytes {
    ($value:literal $(,)?) => {{
        const VALUE: &[u8] = $value;
        const _: () = assert!(VALUE.len() <= $crate::MAX_PLAINTEXT_BYTES, "MindGuard literal exceeds 64 KiB");
        const SITE: $crate::SiteId = $crate::make_site_id(env!("CARGO_PKG_NAME"), file!(), line!(), column!(), 0);
        $crate::ProtectedBytes::new(VALUE, SITE)
    }};
}

#[macro_export]
macro_rules! mg_value {
    (-$value:literal $(,)?) => {{
        const SITE: $crate::SiteId = $crate::make_site_id(env!("CARGO_PKG_NAME"), file!(), line!(), column!(), 0);
        $crate::ProtectedValue::new(-$value, SITE)
    }};
    ($value:literal $(,)?) => {{
        const SITE: $crate::SiteId = $crate::make_site_id(env!("CARGO_PKG_NAME"), file!(), line!(), column!(), 0);
        $crate::ProtectedValue::new($value, SITE)
    }};
}

#[cfg(test)]
mod tests {
    #[test]
    fn dev_api_preserves_exact_values() {
        let mut seen = false;
        crate::mg_with_str!("a\0b", |value: &str| {
            assert_eq!(value.as_bytes(), b"a\0b");
            seen = true;
        });
        assert!(seen);

        crate::mg_with_bytes!(b"\x00\xff", |value: &[u8]| assert_eq!(value, [0, 255]));
        crate::mg_with_value!(-42i32, |value| assert_eq!(value, -42));
        assert_eq!(crate::mg_str!("text").as_str(), "text");
        assert_eq!(crate::mg_bytes!(b"bytes").as_bytes(), b"bytes");
        assert_eq!(crate::mg_value!(7u32).into_inner(), 7);
        assert_ne!(
            crate::make_site_id("target", "src/lib.rs", 1, 2, 0),
            crate::make_site_id("target", "src/lib.rs", 1, 3, 0)
        );
    }
}
