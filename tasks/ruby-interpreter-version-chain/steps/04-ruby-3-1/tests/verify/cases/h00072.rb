begin
  def sq(x) = x * x
  p sq(5)
rescue Exception => e
  p e.class
end
