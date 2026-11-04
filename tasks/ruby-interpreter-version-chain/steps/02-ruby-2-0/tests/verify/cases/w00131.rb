begin
  p((1..4).zip([9]))
rescue Exception => e
  p e.class
end
