use crate::msgs::codec::{Codec, Reader};
use crate::msgs::enums::{AlertDescription, AlertLevel};

#[derive(Debug)]
pub struct AlertMessagePayload {
    pub level: AlertLevel,
    pub description: AlertDescription,
}

impl Codec for AlertMessagePayload {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<AlertMessagePayload> {
        unimplemented!()
    }
}

impl AlertMessagePayload {
    pub fn length(&self) -> usize {
        unimplemented!()
    }
}
