begin
  def f; return 1; 2; end
  p f
rescue Exception => __e
  p __e.class
end
