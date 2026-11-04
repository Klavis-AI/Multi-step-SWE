begin
  def f(**nil); :ok; end
  p f
rescue Exception => e
  p e.class
end
