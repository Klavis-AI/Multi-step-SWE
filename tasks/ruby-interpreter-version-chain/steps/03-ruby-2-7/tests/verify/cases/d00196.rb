begin
  p(Object.new =~ nil)
rescue Exception => e
  p e.class
end
