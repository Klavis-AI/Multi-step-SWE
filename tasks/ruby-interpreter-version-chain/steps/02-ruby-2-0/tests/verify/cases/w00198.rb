begin
  p(({}).inspect)
rescue Exception => e
  p e.class
end
