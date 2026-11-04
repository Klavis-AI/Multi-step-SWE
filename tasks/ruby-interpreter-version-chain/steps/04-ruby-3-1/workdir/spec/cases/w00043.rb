begin
  def f; 1 + 1; 3; end
  p f
rescue Exception => __e
  p __e.class
end
