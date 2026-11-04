begin
  def inner(k:); k; end
  def outer(**) = inner(**)
  p outer(k: 3)
rescue Exception => e
  p e.class
end
