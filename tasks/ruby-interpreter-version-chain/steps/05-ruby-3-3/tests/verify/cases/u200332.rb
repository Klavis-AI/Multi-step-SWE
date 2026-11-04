Bug4438 = '[ruby-core:35364]'
begin
      p(true)
      begin
	p(true)
      rescue
	p(false)
      else
	p(true)
      end
      p(true)
    rescue
      p(false)
    else
      p(true)
    end