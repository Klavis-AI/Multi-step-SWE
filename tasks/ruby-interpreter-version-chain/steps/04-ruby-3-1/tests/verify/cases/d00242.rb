begin
  p(Data.define(:x).new(x: 1).x)
rescue Exception => e
  p e.class
end
