begin
  p Ractor.shareable?(1)
  p Ractor.shareable?("x".freeze)
  p Ractor.shareable?([1, 2])
rescue Exception => e
  p e.class
end
