begin
  p({})
  p({}.to_s)
  p({nil => nil})
rescue Exception => e
  p e.class
end
