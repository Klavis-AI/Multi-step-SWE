begin
  p(nil.to_s.frozen?)
rescue Exception => e
  p e.class
end
