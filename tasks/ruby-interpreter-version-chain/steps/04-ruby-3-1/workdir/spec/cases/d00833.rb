begin
  def f(k: 1); k; end; f(k: 2)
  p :done
rescue Exception => e
  p e.class
end
