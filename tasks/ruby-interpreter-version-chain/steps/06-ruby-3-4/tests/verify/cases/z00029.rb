begin
  m = "abc".match(/b/)
  p m.bytebegin(0)
  p m.byteend(0)
rescue Exception => e
  p e.class
end
