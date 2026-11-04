begin
  p Object.new.untrust.class
rescue Exception => e
  p e.class
end
