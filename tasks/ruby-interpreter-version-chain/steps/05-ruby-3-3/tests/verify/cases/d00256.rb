begin
  p(Module.new.set_temporary_name("X").name)
rescue Exception => e
  p e.class
end
