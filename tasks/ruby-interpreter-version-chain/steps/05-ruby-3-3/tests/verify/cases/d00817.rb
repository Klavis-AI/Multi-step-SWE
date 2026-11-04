begin
  P = Data.define(:x, :y)
  a = P.new(x: 1, y: 2)
  p a.x
  p a.to_h
  p a == P.new(x: 1, y: 2)
  p a.with(y: 9).y
  p a.frozen?
rescue Exception => e
  p e.class
end
