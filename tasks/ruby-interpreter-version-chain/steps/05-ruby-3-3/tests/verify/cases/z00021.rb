begin
  D = Data.define(:x, :y)
  d = D.new(x: 1, y: 2)
  p [d.x, d.y]
  p d.with(y: 9).to_h
rescue Exception => e
  p e.class
end
