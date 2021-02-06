# rcfitness

Rc tools for fitness tracking

## basal metabolic rate (BMR)

Computes the BMR using the Mifflin-St. Jeor equation.

	# bmr for a 33 year old male whose height is 180cm and weights 92kg
	% bmr 92 180 33
	# bmr for a 27 year old female who weights 67kg and is 165cm tall
	% bmr -w 67 165 27

## weight tracking

	# today the scale marks 80kg (careful with obsessing over this)
	% weighin 80

## default session file

The `default.session` file shows a repetitions-per-exercise table,
matching the order in the `exercises` file, ready to be selected and
sent to a [rio(1)](http:/man.9front.org/1/rio) window right after
starting a `workout`.

## about weights

In weightlifting exercises where (dumb|bar|kettle)bells are used, the
weight isn't tracked.  It should be up to you how much you are lifting
and at what pace.  Feel free to extend the scripts to match your
training procedure.
