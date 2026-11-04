begin
  m = "héllo".match(/l+/)
  p m.byteoffset(0)
  p m.bytebegin(0)
  p m.byteend(0)
rescue Exception => e
  p e.class
end
