begin
  P = Data.define(:x, :y)
  p P.new(1, 2).y
  P.new(1)
rescue Exception => e
  p e.class
end
