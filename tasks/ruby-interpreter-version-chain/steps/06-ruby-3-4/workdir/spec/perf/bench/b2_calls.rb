# method calls and instance-variable reads
class Point
  def initialize(x, y); @x = x; @y = y; end
  def x; @x; end
  def y; @y; end
  def dot(o); x * o.x + y * o.y; end
end
a = Point.new(3, 4)
b = Point.new(5, 6)
t = 1
i = 0
while i < 40000
  t = (t * 7 + a.dot(b)) % 1000003
  i += 1
end
p t
