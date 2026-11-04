begin
  p((1..4).each_entry { })
rescue Exception => e
  p e.class
end
