module M
  refine String do
    def shout; upcase; end
  end
end
begin
  p "hi".shout
rescue Exception => e
  p e.class
end
