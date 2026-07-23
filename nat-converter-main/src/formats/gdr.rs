use std::io::{Read, Seek, Write};

use serde::{Deserialize, Serialize};

use super::replay::{Click, GameVersion, Replay, ReplayError};

fn default_bot_name() -> String {
    "NATTIE_CONVERTER".to_string()
}

fn default_bot_ver() -> String {
    "1.0.0".to_string()
}

#[derive(Serialize, Deserialize, Default)]
struct BotInfo {
    #[serde(default = "default_bot_name")]
    pub name: String,
    #[serde(default = "default_bot_ver")]
    pub version: String,
}

fn default_level_name() -> String {
    "Converted".to_string()
}

fn default_level_id() -> u32 {
    12345678
}

#[derive(Serialize, Deserialize, Default)]
struct LevelInfo {
    #[serde(default = "default_level_id")]
    pub id: u32,
    #[serde(default = "default_level_name")]
    pub name: String,
}

#[derive(Serialize, Deserialize, Default)]
struct GDRInput {
    #[serde(default)]
    pub frame: u32,
    #[serde(default, rename = "btn")]
    pub button: i32,
    #[serde(default, rename = "2p")]
    pub p2: bool,
    #[serde(default)]
    pub down: bool,
}

impl From<GDRInput> for Click {
    fn from(value: GDRInput) -> Self {
        Self::from_hold(value.frame, value.down, value.p2)
    }
}

const fn default_fps() -> f32 {
    240.0
}

#[derive(Serialize, Deserialize, Default)]
struct GDRReplay {
    #[serde(rename = "gameVersion", default)]
    pub game_version: f32,
    #[serde(default)]
    pub description: String,
    #[serde(default)]
    pub version: f32,
    #[serde(default)]
    pub duration: f32,
    #[serde(default = "default_bot_name")]
    pub author: String,
    #[serde(default)]
    pub seed: i32,
    #[serde(default)]
    pub coins: i32,
    #[serde(default)]
    pub ldm: bool,

    #[serde(default, rename = "bot")]
    pub bot_info: BotInfo,
    #[serde(default, rename = "level")]
    pub level_info: LevelInfo,

    #[serde(default, rename = "inputs")]
    pub clicks: Vec<GDRInput>,

    #[serde(default = "default_fps", rename = "framerate")]
    pub fps: f32,
}

impl TryFrom<&Replay> for GDRReplay {
    type Error = ReplayError;

    fn try_from(orig: &Replay) -> Result<Self, Self::Error> {
        let dur = (orig.clicks.last().map_or(0, |i| i.frame) as f32) / orig.fps;

        let mut replay = Self {
            fps: orig.fps,
            game_version: 2.204,
            version: 1.0,
            duration: dur,
            author: "CONVERTED MACRO".to_owned(),
            bot_info: BotInfo {
                name: "NATTIE_CONVERTER".to_string(),
                version: "1.0.0".to_string(),
            },
            level_info: LevelInfo {
                id: 12345678,
                name: "LEVEL NAME".to_string(),
            },
            ..Default::default()
        };

        orig.clicks.iter().try_for_each(|click| {
            click.apply_hold(|frame, down, p2| {
                replay.clicks.push(GDRInput {
                    frame,
                    down,
                    p2,
                    button: 1,
                });

                Ok::<(), ReplayError>(())
            })
        })?;

        Ok(replay)
    }
}

impl Replay {
    pub fn parse_gdr(&mut self, reader: impl Read + Seek) -> Result<(), ReplayError> {
        let replay: GDRReplay =
            rmp_serde::from_read(reader).map_err(|_| ReplayError::ParseError)?;

        self.fps = replay.fps.round();
        self.clicks = replay.clicks.into_iter().map(GDRInput::into).collect();
        self.game_version = GameVersion::Version2206;

        Ok(())
    }

    pub fn parse_gdr_json(&mut self, reader: impl Read + Seek) -> Result<(), ReplayError> {
        let replay: GDRReplay =
            serde_json::from_reader(reader).map_err(|_| ReplayError::ParseError)?;

        self.fps = replay.fps.round();
        self.clicks = replay.clicks.into_iter().map(GDRInput::into).collect();
        self.game_version = GameVersion::Version2206;

        Ok(())
    }

    pub fn write_gdr(&self, writer: &mut (impl Write + Seek)) -> Result<(), ReplayError> {
        let replay = GDRReplay::try_from(self)?;

        let mut serializer = rmp_serde::Serializer::new(writer).with_struct_map();

        replay
            .serialize(&mut serializer)
            .map_err(|_| ReplayError::WriteError)?;

        Ok(())
    }

    pub fn write_gdr_json(&self, writer: &mut (impl Write + Seek)) -> Result<(), ReplayError> {
        let replay = GDRReplay::try_from(self)?;

        if self.settings.beautified_json {
            serde_json::to_writer_pretty(writer, &replay).map_err(|_| ReplayError::WriteError)?;
        } else {
            serde_json::to_writer(writer, &replay).map_err(|_| ReplayError::WriteError)?;
        }

        Ok(())
    }
}
