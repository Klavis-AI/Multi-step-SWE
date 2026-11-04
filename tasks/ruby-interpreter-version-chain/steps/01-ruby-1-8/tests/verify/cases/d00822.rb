begin
  p Ractor.shareable?("a")
rescue Exception => e
  p e.class
end
