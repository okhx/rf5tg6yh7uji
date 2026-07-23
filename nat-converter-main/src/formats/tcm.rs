use std::io::{Read, Seek, Write};

use tcm::{input::InputCommand, replay::ReplaySerializer, DynamicReplay};

use crate::formats::replay::{Click, Replay, ReplayError};

impl Replay {
    pub fn parse_tcm(&mut self, reader: impl Read + Seek) -> Result<(), ReplayError> {
        self.game_version = super::replay::GameVersion::Version2206;

        let mut reader = std::io::BufReader::new(reader);
        let replay = DynamicReplay::from_reader(&mut reader)?;

        self.fps = replay.meta.tps();
        self.clicks = replay
            .inputs
            .iter()
            .filter_map(|input| match &input.input {
                tcm::input::Input::Vanilla(i) => {
                    Some(Click::from_hold(input.frame as u32, i.push, i.player2))
                }
                _ => None,
            })
            .collect();

        Ok(())
    }

    pub fn write_tcm(&self, writer: &mut (impl Write + Seek)) -> Result<(), ReplayError> {
        let meta = Box::new(tcm::meta::MetaV2::new(self.fps, 0, None));
        let mut inputs = Vec::new();

        self.clicks.iter().try_for_each(|click| {
            click.apply_hold(|frame, hold, player_2| {
                inputs.push(InputCommand::new(
                    frame as u64,
                    tcm::input::Input::Vanilla(tcm::input::VanillaInput {
                        push: hold,
                        player2: player_2,
                        button: tcm::input::PlayerButton::Jump,
                    }),
                ));

                Ok::<(), ReplayError>(())
            })
        })?;

        let replay = DynamicReplay { meta, inputs };
        let mut writer = std::io::BufWriter::new(writer);
        replay
            .serialize(&mut writer)
            .map_err(|_| ReplayError::WriteError)?;

        Ok(())
    }
}
