begin
  p proc { |x| }.parameters(lambda: true)
  p lambda { |x| }.parameters(lambda: false)
rescue Exception => e
  p e.class
end
