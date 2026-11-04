begin
  p((1..4).each_slice(2) { })
rescue Exception => e
  p e.class
end
