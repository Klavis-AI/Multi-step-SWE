begin
  p(Hash.new.to_s)
rescue Exception => e
  p e.class
end
