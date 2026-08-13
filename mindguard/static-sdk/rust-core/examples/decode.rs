use std::{env, fs, hint::black_box, process, time::Instant};

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() != 6 {
        process::exit(64);
    }
    let blob = fs::read(&args[1]).unwrap_or_else(|_| process::exit(65));
    let share_bytes = fs::read(&args[2]).unwrap_or_else(|_| process::exit(66));
    let site = u64::from_str_radix(args[3].strip_prefix("0x").unwrap_or(&args[3]), 16)
        .unwrap_or_else(|_| process::exit(68));
    let kind = args[4].parse::<u8>().unwrap_or_else(|_| process::exit(69));
    enum Share {
        Raw([u8; 32]),
        Packed(Box<[u8; 256]>),
    }
    let share = match share_bytes.len() {
        32 => Share::Raw(share_bytes.try_into().unwrap()),
        256 => Share::Packed(share_bytes.into_boxed_slice().try_into().unwrap()),
        _ => process::exit(67),
    };
    let decode = || match &share {
        Share::Raw(value) => mindguard_static_core::decode(&blob, value, site, kind),
        Share::Packed(value) => mindguard_static_core::decode_packed(&blob, value, site, kind),
    };
    if args[5] == "--bench" {
        let started = Instant::now();
        let mut checksum = 0u64;
        for iteration in 0..1000 {
            let sample = decode().unwrap_or_else(|_| process::exit(71));
            checksum += sample.as_bytes()[iteration % sample.as_bytes().len()] as u64;
            black_box(&sample);
        }
        println!("{} {}", started.elapsed().as_nanos() / 1000, checksum);
        return;
    }
    let expected = fs::read(&args[5]).unwrap_or_else(|_| process::exit(70));
    let result = decode();
    match result {
        Ok(plaintext) if plaintext.as_bytes() == expected => println!("OK"),
        Ok(_) => process::exit(71),
        Err(error) => {
            println!("{error:?}");
            process::exit(1);
        }
    }
}
