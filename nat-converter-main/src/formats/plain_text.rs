use super::replay::{Click, GameVersion, Replay, ReplayError};
use std::io::{BufRead, Read, Seek, Write};

impl Replay {
    pub fn parse_plain_text(&mut self, reader: impl Read + Seek) -> Result<(), ReplayError> {
        let reader = std::io::BufReader::new(reader);
        let mut lines = reader.lines();

        self.game_version = GameVersion::Any;
        self.fps = lines
            .next()
            .ok_or(ReplayError::ParseError)?
            .map_err(|_| ReplayError::ParseError)?
            .parse::<f32>()
            .map_err(|_| ReplayError::ParseError)?;

        lines.try_for_each(|line| {
            let line = line.map_err(|_| ReplayError::ParseError)?;
            let mut data = line.split_whitespace();

            let frame = data
                .next()
                .ok_or(ReplayError::ParseError)?
                .parse::<u32>()
                .map_err(|_| ReplayError::ParseError)?;

            let hold = data.next().ok_or(ReplayError::ParseError)? == "1";

            let player_2 = data.next().ok_or(ReplayError::ParseError)? == "1";

            self.clicks.push(Click::from_hold(frame, hold, player_2));

            Ok::<(), ReplayError>(())
        })?;

        Ok(())
    }

    pub fn write_plain_text(&self, writer: &mut (impl Write + Seek)) -> Result<(), ReplayError> {
        let mut writer = std::io::BufWriter::new(writer);

        writer
            .write_all(format!("{}\n", self.fps).as_bytes())
            .map_err(|_| ReplayError::WriteError)?;

        self.clicks.iter().try_for_each(|click| {
            click.apply_hold(|frame, hold, p2| {
                writer
                    .write_all(format!("{} {} {}\n", frame, hold as i32, p2 as i32).as_bytes())?;

                Ok::<(), ReplayError>(())
            })
        })?;

        Ok(())
    }
}
