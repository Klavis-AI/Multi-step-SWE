begin
  p(("a".."e").step(2).to_a)
rescue Exception => e
  p e.class
end
