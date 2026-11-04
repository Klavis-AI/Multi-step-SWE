begin
  puts({x: 1}.to_s)
rescue Exception => e
  p e.class
end
