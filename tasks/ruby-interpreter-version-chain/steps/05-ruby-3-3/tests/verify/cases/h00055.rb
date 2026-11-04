begin
  def inner(a, b); [a, b]; end
  def outer(*) = inner(*)
  p outer(1, 2)
rescue Exception => e
  p e.class
end
