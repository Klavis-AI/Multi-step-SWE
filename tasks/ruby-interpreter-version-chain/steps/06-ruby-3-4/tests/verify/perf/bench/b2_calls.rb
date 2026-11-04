# method calls and instance-variable reads
class Point
  def initialize(x, y); @x = x; @y = y; end
  def x; @x; end
  def y; @y; end
  def dot(o); x * o.x + y * o.y; end
end
a = Point.new(2, 9)
b = Point.new(8, 5)
t = 3
i = 0
while i < 40000
  t = (t * 11 + a.dot(b)) % 999983
  i += 1
end
p t
