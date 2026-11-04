begin
  p(Object.new.trust)
rescue Exception => e
  p e.class
end
