import madmom
import numpy as np
import matplotlib.pyplot as plt
from scipy.ndimage.filters import maximum_filter
#SuperFlux onset detection algorithm is used for this tutorial. It is based on the well known spectral flux algorithm, so we do this first. The spectral flux is defined as the sum (or mean) of the first order differences of the magnitude spectrogram of an audio file

# use madmom to get the spectrogram
spec = madmom.audio.spectrogram.Spectrogram('Data/sample.wav')
print('a')



# calculate the difference
diff = np.diff(spec, axis=0)
# keep only the positive differences
pos_diff = np.maximum(0, diff)
# sum everything to get the spectral flux
sf = np.sum(pos_diff, axis=1)



plt.figure()
plt.imshow(spec[:, :200].T, origin='lower', aspect='auto')
plt.figure()
plt.imshow(pos_diff[:, :200].T, origin='lower', aspect='auto')
plt.figure()
plt.plot(sf)



sf = madmom.features.onsets.spectral_flux(spec)

"""Spectral flux works quite well for onset detection,
but to make this tutorial a bit more interesting,
we add the SuperFlux stuff."""


#Part B
"""To achieve the same with madmom, some classes have some of
the needed functionality already built in for convienience. E.g.
the LogarithmicFilteredSpectrogram does all the filtering and scaling
combined in a single class (accepting a STFT, FramedSignal, S
ignal object or a file name as input). Appropriate arguments can be given and the ones not
understood by the class are passed if there's the need to instantiate an object up the hierarchy."""


def sp(filename):
    log_filt_spec = madmom.audio.spectrogram.LogarithmicFilteredSpectrogram(filename, num_bands=24)
    superflux = madmom.features.onsets.superflux(log_filt_spec)
    return superflux

def main():
    return sp('Data/sample.wav')


