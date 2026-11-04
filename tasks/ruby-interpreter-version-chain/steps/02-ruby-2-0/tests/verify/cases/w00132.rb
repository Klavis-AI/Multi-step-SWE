begin
  p((1..4).take_while { true })
rescue Exception => e
  p e.class
end
