#![warn(clippy::all, clippy::pedantic, clippy::nursery, clippy::cargo)]
// Missing const for fn is allowed because #[wasm_bindgen] requires non-const functions
#![allow(
    clippy::missing_errors_doc,
    clippy::missing_const_for_fn,
    clippy::cast_lossless,
    clippy::cast_sign_loss,
    clippy::cast_possible_truncation,
    clippy::cast_possible_wrap,
    clippy::cast_precision_loss,
    clippy::module_name_repetitions,
    clippy::cargo_common_metadata
)]

pub mod formats;

use std::io::Cursor;

use formats::replay::{Click, ClickType, GameVersion, Replay};
use thiserror::Error;
use wasm_bindgen::prelude::*;

#[wasm_bindgen]
#[derive(Clone, Copy, PartialEq, Eq)]
pub struct Settings {
    pub auto_offset: bool,
    pub beautified_json: bool,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            auto_offset: true,
            beautified_json: true,
        }
    }
}

#[wasm_bindgen]
#[derive(Clone, Default)]
pub struct Converter {
    loaded_replay: Replay,
    pub settings: Settings,
}

#[wasm_bindgen]
#[derive(Clone, Copy)]
pub enum Format {
    OmegaBot,
    OmegaBot2,
    URL,
    MHRJson,
    MHRBinary,
    Tasbot,
    ZBot,
    ReplayBot,
    Fembot,
    EchoOld,
    EchoNewJson,
    EchoNewBinary,
    YBot1,
    XBot,
    Rush,
    KDBot,
    YBot,
    GDR,
    GDRJson,
    XDBot,
    Silicate,
    Slc2,
    Slc3,
    Tcm,
    PlainText,
}

#[wasm_bindgen]
#[derive(Debug, Error)]
pub enum ConverterError {
    #[error("Invalid data provided")]
    InvalidData,
}

#[wasm_bindgen]
extern "C" {
    #[wasm_bindgen(js_namespace = console, js_name = error)]
    fn console_error(s: &str);

    #[wasm_bindgen(js_namespace = console, js_name = log)]
    fn console_log(s: &str);
}

#[wasm_bindgen]
#[derive(Clone, Copy)]
pub enum CPSRule {
    Rule15CPS,
    Rule3CPF,
    Rule45CP5C,
}

#[wasm_bindgen]
pub struct CPSViolation {
    pub rule: CPSRule,
    pub frame: u32,
    pub cps: f64,
}

#[wasm_bindgen]
impl Converter {
    pub fn load(&mut self, data: Vec<u8>, fmt: Format) -> Result<(), ConverterError> {
        let cursor = Cursor::new(data);

        self.loaded_replay.clear();
        self.loaded_replay.settings = self.settings;

        let result = match fmt {
            Format::PlainText => self.loaded_replay.parse_plain_text(cursor),
            Format::Tasbot => self.loaded_replay.parse_tasbot(cursor),
            Format::ZBot => self.loaded_replay.parse_zbot(cursor),
            Format::OmegaBot => self.loaded_replay.parse_obot3(cursor),
            Format::OmegaBot2 => self.loaded_replay.parse_obot2(cursor),
            Format::URL => self.loaded_replay.parse_url(cursor),
            Format::MHRJson => self.loaded_replay.parse_mhr_json(cursor),
            Format::MHRBinary => self.loaded_replay.parse_mhr_binary(cursor),
            Format::YBot => self.loaded_replay.parse_ybot2(cursor),
            Format::GDR => self.loaded_replay.parse_gdr(cursor),
            Format::GDRJson => self.loaded_replay.parse_gdr_json(cursor),
            Format::ReplayBot => self.loaded_replay.parse_replaybot(cursor),
            Format::YBot1 => self.loaded_replay.parse_ybot1(cursor),
            Format::EchoOld => self.loaded_replay.parse_echo_old(cursor),
            Format::EchoNewJson => self.loaded_replay.parse_echo_new(cursor),
            Format::EchoNewBinary => self.loaded_replay.parse_echo_bin(cursor),
            Format::Rush => self.loaded_replay.parse_rush(cursor),
            Format::KDBot => self.loaded_replay.parse_kdbot(cursor),
            Format::XBot => self.loaded_replay.parse_xbot(cursor),
            Format::XDBot => self.loaded_replay.parse_xdbot(cursor),
            Format::Fembot => self.loaded_replay.parse_fembot(cursor),
            Format::Silicate => self.loaded_replay.parse_silicate(cursor),
            Format::Slc2 => self.loaded_replay.parse_slc2(cursor),
            Format::Slc3 => self.loaded_replay.parse_slc3(cursor),
            Format::Tcm => self.loaded_replay.parse_tcm(cursor),
        };

        match result {
            Ok(()) => {}
            Err(e) => {
                console_error(&e.to_string());
            }
        }

        Ok(())
    }

    #[must_use]
    pub fn get_fps(&self) -> f32 {
        self.loaded_replay.fps
    }

    pub fn set_fps(&mut self, fps: f32) {
        self.loaded_replay.fps = fps;
    }

    pub fn set_setting_beautify_json(&mut self, value: bool) {
        self.settings.beautified_json = value;
    }

    pub fn set_setting_auto_offset(&mut self, value: bool) {
        self.settings.auto_offset = value;
    }

    #[must_use]
    pub fn length(&self) -> usize {
        self.loaded_replay.clicks.len()
    }

    // pub fn clicks(&self) -> Vec<Click> {
    //     self.loaded_replay.clicks.clone()
    // }

    #[must_use]
    pub fn game_version(&self) -> GameVersion {
        self.loaded_replay.game_version
    }

    #[must_use]
    pub fn clicks(&self) -> Vec<Click> {
        self.loaded_replay.clicks.clone()
    }

    #[must_use]
    pub fn click_at(&self, idx: usize) -> Click {
        self.loaded_replay.clicks[idx]
    }

    pub fn offset_all_by(&mut self, offset: i64) {
        let clicks_old = self.loaded_replay.clicks.clone();

        self.loaded_replay.clicks = clicks_old
            .into_iter()
            .map(|click| {
                let new_frame = if offset < -(click.frame as i64) {
                    0
                } else {
                    ((click.frame as i64) + offset) as u32
                };

                Click {
                    frame: new_frame,
                    ..click
                }
            })
            .collect();
    }

    #[must_use]
    pub fn clicks_at_batch(&self, idx: usize, page: usize) -> Vec<Click> {
        if self.loaded_replay.clicks.is_empty() || idx >= self.loaded_replay.clicks.len() {
            return vec![];
        }

        if idx + page >= self.loaded_replay.clicks.len() {
            return self.loaded_replay.clicks[idx..self.loaded_replay.clicks.len()].to_vec();
        }

        self.loaded_replay.clicks[idx..idx + page].to_vec()
    }

    pub fn replace_frame_at(&mut self, idx: usize, frame: u32) {
        self.loaded_replay.clicks[idx].frame = frame;
    }

    pub fn insert_empty_at(&mut self, idx: usize, frame: u32) {
        self.loaded_replay.clicks.insert(
            idx,
            Click {
                frame,
                p1: ClickType::Skip,
                p2: ClickType::Skip,
            },
        );
    }

    pub fn remove_at(&mut self, idx: usize) {
        self.loaded_replay.clicks.remove(idx);
    }

    pub fn toggle_click_at(&mut self, idx: usize, player_2: bool) {
        if player_2 {
            self.loaded_replay.clicks[idx].p2 = self.loaded_replay.clicks[idx].p2.toggle();
        } else {
            self.loaded_replay.clicks[idx].p1 = self.loaded_replay.clicks[idx].p1.toggle();
        }
    }

    pub fn clean(&mut self) {
        let mut current_click_state_p1 = false;
        let mut current_click_state_p2 = false;

        let clicks_old = self.loaded_replay.clicks.clone();
        self.loaded_replay.clicks = clicks_old
            .into_iter()
            .map(|click| {
                let mut new_click = click;

                if click.p1.is_click() {
                    let valid = !current_click_state_p1;
                    current_click_state_p1 = true;
                    if !valid {
                        console_log(&format!(
                            "CLEANED {} - turned reduntant p1 click into skip",
                            click.frame
                        ));
                        new_click.p1 = ClickType::Skip;
                    }
                } else if click.p1.is_release() {
                    let valid = current_click_state_p1;
                    current_click_state_p1 = false;
                    if !valid {
                        console_log(&format!(
                            "CLEANED {} - turned reduntant p1 release into skip",
                            click.frame
                        ));
                        new_click.p1 = ClickType::Skip;
                    }
                }

                if click.p2.is_click() {
                    let valid = !current_click_state_p2;
                    current_click_state_p2 = if valid { true } else { current_click_state_p2 };
                    if !valid {
                        console_log(&format!(
                            "CLEANED {} - turned reduntant p2 click into skip",
                            click.frame
                        ));
                        new_click.p2 = ClickType::Skip;
                    }
                } else if click.p2.is_release() {
                    let valid = current_click_state_p2;
                    current_click_state_p2 = false;
                    if !valid {
                        console_log(&format!(
                            "CLEANED {} - turned reduntant p2 release into skip",
                            click.frame
                        ));
                        new_click.p2 = ClickType::Skip;
                    }
                }

                new_click
            })
            .filter(|click| {
                if click.p1.is_skip() && click.p2.is_skip() {
                    console_log(&format!(
                        "CLEANED {} - removed input as both p1 and p2 were skips",
                        click.frame
                    ));
                    return false;
                }

                true
            })
            .collect();

        console_log("Successfully cleaned replay");
    }

    pub fn sort(&mut self) {
        self.loaded_replay
            .clicks
            .sort_by(|c1, c2| c1.frame.cmp(&c2.frame));

        console_log("Successfully sorted inputs");
    }

    pub fn remove_all_player_inputs(&mut self, player_2: bool) {
        let clicks_old = self.loaded_replay.clicks.clone();

        self.loaded_replay.clicks = clicks_old
            .into_iter()
            .map(|click| {
                let mut new_click = click;

                if player_2 {
                    new_click.p2 = ClickType::Skip;
                } else {
                    new_click.p1 = ClickType::Skip;
                }

                new_click
            })
            .filter(|click| {
                if click.p1.is_skip() && click.p2.is_skip() {
                    console_log(&format!(
                        "CLEANED {} - removed input as both p1 and p2 were skips",
                        click.frame
                    ));
                    return false;
                }

                true
            })
            .collect();
    }

    pub fn flip_p1_p2(&mut self) {
        let clicks_old = self.loaded_replay.clicks.clone();

        self.loaded_replay.clicks = clicks_old
            .into_iter()
            .map(|click| {
                let mut new_click = click;

                new_click.p2 = click.p1;
                new_click.p1 = click.p2;

                new_click
            })
            .collect();
    }

    pub fn flip_up_down(&mut self) {
        let clicks_old = self.loaded_replay.clicks.clone();

        self.loaded_replay.clicks = clicks_old
            .into_iter()
            .map(|click| {
                let mut new_click = click;

                new_click.p1 = match click.p1 {
                    ClickType::Skip => ClickType::Skip,
                    ClickType::Click => ClickType::Release,
                    ClickType::Release => ClickType::Click,
                };
                new_click.p2 = match click.p2 {
                    ClickType::Skip => ClickType::Skip,
                    ClickType::Click => ClickType::Release,
                    ClickType::Release => ClickType::Click,
                };

                new_click
            })
            .collect();
    }

    pub fn save(&mut self, fmt: Format) -> Vec<u8> {
        let buffer = Vec::new();
        let mut cursor = Cursor::new(buffer);

        self.loaded_replay.settings = self.settings;
        if self.settings.beautified_json {
            console_log("is beautified");
        }

        let result = match fmt {
            Format::PlainText => self.loaded_replay.write_plain_text(&mut cursor),
            Format::Tasbot => self.loaded_replay.write_tasbot(&mut cursor),
            Format::ZBot => self.loaded_replay.write_zbot(&mut cursor),
            Format::OmegaBot => self.loaded_replay.write_obot3(&mut cursor),
            Format::OmegaBot2 => self.loaded_replay.write_obot2(&mut cursor),
            Format::URL => self.loaded_replay.write_url(&mut cursor),
            Format::MHRJson => self.loaded_replay.write_mhr_json(&mut cursor),
            Format::MHRBinary => self.loaded_replay.write_mhr_binary(&mut cursor),
            Format::YBot => self.loaded_replay.write_ybot2(&mut cursor),
            Format::GDR => self.loaded_replay.write_gdr(&mut cursor),
            Format::GDRJson => self.loaded_replay.write_gdr_json(&mut cursor),
            Format::ReplayBot => self.loaded_replay.write_replaybot(&mut cursor),
            Format::YBot1 => self.loaded_replay.write_ybot1(&mut cursor),
            Format::EchoOld => self.loaded_replay.write_echo_old(&mut cursor),
            Format::EchoNewJson => self.loaded_replay.write_echo_new(&mut cursor),
            Format::EchoNewBinary => self.loaded_replay.write_echo_bin(&mut cursor),
            Format::Rush => self.loaded_replay.write_rush(&mut cursor),
            Format::KDBot => self.loaded_replay.write_kdbot(&mut cursor),
            Format::XBot => self.loaded_replay.write_xbot(&mut cursor),
            Format::XDBot => self.loaded_replay.write_xdbot(&mut cursor),
            Format::Fembot => self.loaded_replay.write_fembot(&mut cursor),
            Format::Silicate => self.loaded_replay.write_silicate(&mut cursor),
            Format::Slc2 => self.loaded_replay.write_slc2(&mut cursor),
            Format::Slc3 => self.loaded_replay.write_slc3(&mut cursor),
            Format::Tcm => self.loaded_replay.write_tcm(&mut cursor),
        };

        match result {
            Ok(()) => {}
            Err(e) => {
                console_error(&e.to_string());
            }
        }

        cursor.into_inner()
    }

    fn check_cps_for_player(&self, p2: bool) -> Vec<CPSViolation> {
        let mut clicked_frames: Vec<u32> = vec![];
        let mut violations = vec![];

        self.loaded_replay.clicks.iter().for_each(|click| {
            if p2 && !click.p2.is_click() {
                return;
            }

            if !p2 && !click.p1.is_click() {
                return;
            }

            let old_clicked_frames = clicked_frames.clone();
            clicked_frames = old_clicked_frames
                .into_iter()
                .filter(|frame| click.frame - frame < (self.loaded_replay.fps as u32))
                .collect();

            clicked_frames.push(click.frame);

            if clicked_frames.len() > 16 {
                violations.push(CPSViolation {
                    rule: CPSRule::Rule15CPS,
                    frame: click.frame,
                    cps: clicked_frames.len() as f64,
                });
            }

            let just_this_frame = clicked_frames.iter().filter(|f| **f == click.frame);

            if just_this_frame.count() > 3 {
                violations.push(CPSViolation {
                    rule: CPSRule::Rule3CPF,
                    frame: click.frame,
                    cps: clicked_frames.len() as f64,
                });
            }

            let max_frame_diff = (self.loaded_replay.fps / 48.0).ceil() as u32;

            let mut last_5_clicks = clicked_frames.clone();
            if last_5_clicks.len() < 6 {
                return;
            }
            let last_5_clicks: Vec<u32> =
                last_5_clicks.drain(0..(last_5_clicks.len() - 5)).collect();

            let mut last_click: u32 = 0;

            let violates = last_5_clicks.iter().all(|c| {
                let result = c - last_click < max_frame_diff;
                last_click = *c;

                result
            });

            if violates {
                violations.push(CPSViolation {
                    rule: CPSRule::Rule45CP5C,
                    frame: click.frame,
                    cps: clicked_frames.len() as f64,
                });
            }
        });

        violations
    }

    pub fn check_cps(&self) {
        let mut violations = self.check_cps_for_player(false);
        let mut violations_p2 = self.check_cps_for_player(true);
        violations.append(&mut violations_p2);

        if violations.is_empty() {
            console_log("No violations found. This macro complies with every ILL CPS rule.");
        }

        violations
            .iter()
            .for_each(|violation| match violation.rule {
                CPSRule::Rule15CPS => console_log(&format!(
                    "FRAME {}: {} CPS! Exceeded 16 CPS",
                    violation.frame, violation.cps
                )),
                CPSRule::Rule3CPF => console_log(&format!(
                    "FRAME {}: {} CPS! Exceeded 3 clicks per frame",
                    violation.frame, violation.cps
                )),
                CPSRule::Rule45CP5C => console_log(&format!(
                    "FRAME {}: {} CPS! Exceeded 48 CPS in a burst of 5 inputs",
                    violation.frame, violation.cps
                )),
            });
    }

    #[wasm_bindgen(constructor)]
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }
}

#[wasm_bindgen(start)]
fn run() {
    console_error_panic_hook::set_once();

    let window = web_sys::window().expect("no global `window` exists");
    let document = window.document().expect("should have a document on window");
    let _body = document.body().expect("document should have a body");
}
