begin
  p(({a: 1}).inspect)
rescue Exception => e
  p e.class
end
