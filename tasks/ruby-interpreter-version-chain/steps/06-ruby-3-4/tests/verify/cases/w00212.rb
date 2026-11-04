begin
  p(({a: 1}).to_a)
rescue Exception => e
  p e.class
end
