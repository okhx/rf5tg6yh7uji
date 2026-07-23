use std::io::{BufReader, BufWriter, Read, Seek, Write};

use slc_oxide::{
    input::{InputData, PlayerInput},
    meta::Meta,
    v3::{
        atom::{AtomId, AtomVariant},
        builtin::ActionAtom,
        Action, ActionType, Metadata,
    },
};

use super::replay::{Click, GameVersion, Replay, ReplayError};

struct Slc2Meta {
    reserved: [u8; 64],
}

impl Meta for Slc2Meta {
    fn from_bytes(bytes: &[u8]) -> Self {
        let mut reserved = [0; 64];
        reserved.copy_from_slice(&bytes[..64]);
        Slc2Meta { reserved }
    }

    fn size() -> u64 {
        64
    }

    fn to_bytes(&self) -> Box<[u8]> {
        let mut bytes = vec![0; 64];
        bytes[..64].copy_from_slice(&self.reserved);
        bytes.into_boxed_slice()
    }
}

impl Replay {
    pub fn parse_silicate(&mut self, reader: impl Read + Seek) -> Result<(), ReplayError> {
        self.game_version = GameVersion::Version2206;

        let mut reader = BufReader::new(reader);

        let mut fps = [0; 8];
        reader.read_exact(&mut fps)?;
        self.fps = f64::from_le_bytes(fps) as f32;

        let mut input_count = [0; 4];
        reader.read_exact(&mut input_count)?;
        let input_count = u32::from_le_bytes(input_count);

        self.clicks.reserve(input_count as usize);

        for _ in 0..input_count {
            let mut state = [0; 4];
            reader.read_exact(&mut state)?;

            let state = u32::from_le_bytes(state);

            let frame = (state >> 4) as u32;
            let player_2 = (state & 0b1000) != 0;
            match (state & 0b0110) >> 1 {
                1 => {}
                _ => {
                    continue;
                }
            };

            let down = (state & 0b0001) != 0;

            self.clicks.push(Click::from_hold(frame, down, player_2));
        }

        Ok(())
    }

    pub fn parse_slc2(&mut self, reader: impl Read + Seek) -> Result<(), ReplayError> {
        self.game_version = GameVersion::Version2206;

        let mut reader = BufReader::new(reader);
        let replay = slc_oxide::replay::Replay::<Slc2Meta>::read(&mut reader)?;

        self.fps = replay.tps as f32;
        self.clicks.clear();
        self.clicks.reserve(replay.inputs.len());

        for click in replay.inputs {
            match click.data {
                InputData::Player(p) => {
                    if p.button != 1 {
                        continue;
                    }

                    self.clicks
                        .push(Click::from_hold(click.frame as u32, p.hold, p.player_2));
                }
                _ => {}
            }
        }

        Ok(())
    }

    pub fn parse_slc3(&mut self, reader: impl Read + Seek) -> Result<(), ReplayError> {
        self.game_version = GameVersion::Version2206;

        let mut reader = BufReader::new(reader);
        let replay = slc_oxide::v3::Replay::read(&mut reader)?;

        self.fps = replay.metadata.tps as f32;
        self.clicks.clear();
        let action_atom = replay
            .atoms
            .atoms
            .iter()
            .find(|atom| atom.id() == AtomId::Action)
            .ok_or(ReplayError::ParseError)?;

        let AtomVariant::Action(actions) = action_atom else {
            return Err(ReplayError::ParseError);
        };

        for action in &actions.actions {
            if action.action_type == ActionType::Jump {
                let Action {
                    frame,
                    player2,
                    holding,
                    ..
                } = action;

                self.clicks
                    .push(Click::from_hold(*frame as u32, *holding, *player2));
            }
        }

        Ok(())
    }

    pub fn write_slc3(&self, writer: &mut (impl Write + Seek)) -> Result<(), ReplayError> {
        let mut writer = BufWriter::new(writer);

        let mut replay = slc_oxide::v3::Replay::new(Metadata::new(self.fps as f64, 0, 1));
        replay.metadata.tps = self.fps as f64;

        let mut atom = ActionAtom::new();

        self.clicks.iter().try_for_each(|click| {
            click.apply_hold(|frame, hold, player_2| {
                let _ = atom.add_player_action(frame.into(), ActionType::Jump, hold, player_2);

                Ok::<(), ReplayError>(())
            })
        })?;

        replay.add_atom(AtomVariant::Action(atom));

        replay.write(&mut writer)?;

        Ok(())
    }

    pub fn write_slc2(&self, writer: &mut (impl Write + Seek)) -> Result<(), ReplayError> {
        let mut writer = BufWriter::new(writer);

        let meta = Slc2Meta { reserved: [0; 64] };
        let mut replay = slc_oxide::replay::Replay::<Slc2Meta>::new(self.fps as f64, meta);

        replay.inputs.reserve(self.clicks.len());

        self.clicks.iter().try_for_each(|click| {
            click.apply_hold(|frame, hold, player_2| {
                replay.add_input(
                    frame as u64,
                    InputData::Player(PlayerInput {
                        hold,
                        player_2,
                        button: 1,
                    }),
                );

                Ok::<(), ReplayError>(())
            })
        })?;

        replay.write(&mut writer)?;

        Ok(())
    }

    pub fn write_silicate(&self, writer: &mut (impl Write + Seek)) -> Result<(), ReplayError> {
        let mut writer = BufWriter::new(writer);

        writer.write_all(&(self.fps as f64).to_le_bytes())?;
        writer.write_all(&(self.clicks.len() as u32).to_le_bytes())?;

        self.clicks.iter().try_for_each(|click| {
            click.apply_hold(|frame, hold, p2| {
                let mut state: u32 = 0;

                state |= frame << 4;
                state |= if p2 { 1 } else { 0 } << 3;
                state |= 1 << 1;
                state |= if hold { 1 } else { 0 };

                writer.write_all(&state.to_le_bytes())?;

                Ok::<(), ReplayError>(())
            })
        })?;

        Ok(())
    }
}
