begin
  m = Module.new
  p m.name
  m.set_temporary_name("Tmp")
  p m.name
rescue Exception => e
  p e.class
end
