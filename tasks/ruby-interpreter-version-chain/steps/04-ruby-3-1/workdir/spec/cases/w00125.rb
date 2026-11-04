begin
  p((1..4).each_with_index { })
rescue Exception => e
  p e.class
end
