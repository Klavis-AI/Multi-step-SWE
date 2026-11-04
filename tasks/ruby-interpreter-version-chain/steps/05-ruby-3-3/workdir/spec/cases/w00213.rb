begin
  p(({a: 1}).keys)
rescue Exception => e
  p e.class
end
