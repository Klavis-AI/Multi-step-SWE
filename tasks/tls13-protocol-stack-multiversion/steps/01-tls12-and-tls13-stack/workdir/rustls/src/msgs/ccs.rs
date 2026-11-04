use crate::msgs::codec::{Codec, Reader};

#[derive(Debug)]
pub struct ChangeCipherSpecPayload;

impl Codec for ChangeCipherSpecPayload {
    fn encode(&self, bytes: &mut Vec<u8>) {
        unimplemented!()
    }

    fn read(r: &mut Reader) -> Option<ChangeCipherSpecPayload> {
        unimplemented!()
    }
}

impl ChangeCipherSpecPayload {
    pub fn length(&self) -> usize {
        unimplemented!()
    }
}
