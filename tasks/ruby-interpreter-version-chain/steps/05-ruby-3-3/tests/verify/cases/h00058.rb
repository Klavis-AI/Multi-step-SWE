begin
  P = Data.define(:x, :y)
  a = P.new(x: 1, y: 2)
  p a
  p a.to_h
  p a.members
  p a.with(y: 9)
  p a.frozen?
  p(a == P.new(x: 1, y: 2))
rescue Exception => e
  p e.class
end
