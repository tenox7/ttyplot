# Fish completion for ttyplot
# https://github.com/tenox7/ttyplot

complete -c ttyplot -f

# Boolean flags
complete -c ttyplot -s 2 -d 'Read two values and draw two plots'
complete -c ttyplot -s b -d 'Use braille line drawing (experimental)'
complete -c ttyplot -s B -d 'Use block element line drawing (experimental)'
complete -c ttyplot -s f -d 'Fill area under the line (experimental)'
complete -c ttyplot -s r -d 'Calculate counter rate'
complete -c ttyplot -s v -d 'Print version and exit'
complete -c ttyplot -s h -d 'Print help and exit'

# Flags requiring arguments
complete -c ttyplot -s c -d 'Plot character' -x
complete -c ttyplot -s e -d 'Error character for values exceeding hardmax' -x
complete -c ttyplot -s E -d 'Error character for values below hardmin' -x
complete -c ttyplot -s s -d 'Soft maximum (initial, grows with input)' -x
complete -c ttyplot -s S -d 'Soft minimum (initial, shrinks with input)' -x
complete -c ttyplot -s m -d 'Hard maximum value limit' -x
complete -c ttyplot -s M -d 'Hard minimum value limit' -x
complete -c ttyplot -s t -d 'Plot title' -x
complete -c ttyplot -s u -d 'Unit label for vertical axis' -x
complete -c ttyplot -s C -d 'Color spec (0-7, dark1, dark2, light1, light2)' -x -a '{dark1,dark2,light1,light2}'