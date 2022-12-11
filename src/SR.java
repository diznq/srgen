import java.awt.image.BufferedImage;
import java.io.File;
import java.io.IOException;
import javax.imageio.ImageIO;
import java.util.*;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

class SR {

	interface ColorScheme {
		double[] translate(int color);
		int channels();
	}

	public static class RGB1 implements ColorScheme {

		public RGB1() {}

		@Override
		public double[] translate(int l) {
			double arr[] = new double[1];
			arr[0] = ((((l >> 16) & 255) + ((l >> 8) & 255) + (l & 255)) / 3);
			return arr;
		}

		@Override
		public int channels() {
			return 1;
		}
	}

	public static class YUV1 implements ColorScheme {
		public YUV1() {}
		@Override
		public double[] translate(int l) {
			double arr[] = new double[1];
			double r = 0.299 * (double) ((l >> 16) & 255);
			double g = 0.587 * (double) ((l >> 8) & 255);
			double b = 0.114 * (double) ((l) & 255);
			double yval = r + g + b;
			arr[0] = ((int) yval);
			return arr;
		}

		@Override
		public int channels() {
			return 1;
		}
	}

	public static class YUV2 implements ColorScheme {
		public YUV2() {}
		@Override
		public double[] translate(int l) {
			double arr[] = new double[2];
			double r = 0.299 * (double) ((l >> 16) & 255);
			double g = 0.587 * (double) ((l >> 8) & 255);
			double b = 0.114 * (double) ((l) & 255);
			double yval = r + g + b;
			double angle = Math.PI * yval / 255.0;
			arr[0] = Math.sin(angle);
			arr[1] = Math.cos(angle);
			return arr;
		}

		@Override
		public int channels() {
			return 2;
		}
	}

	public static class RGB3 implements ColorScheme {
		public RGB3() {}
		@Override
		public double[] translate(int l) {
			double[] col = new double[3];
			col[0] = ((l >> 16) & 255);
			col[1] = ((l >> 8) & 255);
			col[2] = (l & 255);
			return col;
		}

		@Override
		public int channels() {
			return 3;
		}
	}
	
	public static class RGB6 implements ColorScheme {
		public RGB6() {}
		@Override
		public double[] translate(int l) {
			double col[] = new double[6];
			col[0] = Math.PI * ((l >> 16) & 255) / 255.0;
			col[1] = Math.PI * ((l >> 8) & 255) / 255.0;
			col[2] = Math.PI * (l & 255) / 255.0;
			col[3] = Math.cos(col[0]);
			col[4] = Math.cos(col[1]);
			col[5] = Math.cos(col[2]);
			col[0] = Math.sin(col[0]);
			col[1] = Math.sin(col[1]);
			col[2] = Math.sin(col[2]);
			return col;
		}

		@Override
		public int channels() {
			return 6;
		}
	}

	class Prototype {
		public int hits;
		public double[] data;

		public Prototype(int size, int channels) {
			data = new double[size * channels];
		}

		public static int compare(Prototype a, Prototype b) {
			return -Integer.compare(a.hits, b.hits);
		}
	}

	boolean blend = false;
	boolean yuv = true;
	boolean mae = false;
	int noise = 0;
	int blk = 0;
	int blockSize = 5;
	int transforms = 6;
	ColorScheme activeScheme = new RGB6();

	List<Prototype> prototypes = null;
	int[][] buckets = null;

	ExecutorService executorService = Executors.newWorkStealingPool();

	Random rand = new Random();

	private static SR instance = new SR();

	SR() {
		// ...
	}

	public static SR getInstance() {
		return instance;
	}


	static double[] RGBtoYUV(final int in) {
		double r = ((in >> 16) & 255) / 255.0;
		double g = ((in >> 8) & 255) / 255.0;
		double b = ((in) & 255) / 255.0;

		double[] yuv = new double[3];
        double y,u,v;
        
        y = (double)(0.299 * r + 0.587 * g + 0.114 * b);
        u = (double)(-0.14713 * r - 0.28886 * g + 0.436 * b);
        v = (double)(0.615 * r - 0.51499 * g - 0.10001 * b);
        
        yuv[0] = Math.min(1.0, Math.max(0.0, y));
        yuv[1] = Math.min(0.5, Math.max(-0.5, u));
        yuv[2] = Math.min(0.5, Math.max(-0.5, v));

		return yuv;
	}


	static int YUVtoRGB(final double yuv[]) {
		int[] rgb = new int[3];
        double r,g,b;
		double y = yuv[0], u = yuv[1], v = yuv[2];
        
        r = (double)((y + 0.000 * u + 1.140 * v) * 255);
        g = (double)((y - 0.396 * u - 0.581 * v) * 255);
        b = (double)((y + 2.029 * u + 0.000 * v) * 255);
        
        rgb[0] = Math.max(0, Math.min(255, (int)r));
        rgb[1] = Math.max(0, Math.min(255, (int)g));
        rgb[2] = Math.max(0, Math.min(255, (int)b));

		return (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
	}

	int mixColor(final int clr1, final int clr2) {
		double[] Yuv1 = RGBtoYUV(clr1);
		double[] Yuv2 = RGBtoYUV(clr2);
		Yuv2[0] = Yuv1[0];
		return YUVtoRGB(Yuv2);
	}

	int transformBlock(final BufferedImage img, final int x, final int y, final int w, final int h, final double[] out, final int[] bucket, final int dir) {
		int c = 0, C = 0, tx = 0, ty = 0;
		for (int _y = 0; _y < h; _y++) {
			for (int _x = 0; _x < w; _x++) {

				switch (dir) {
					case 0:
						tx = x + _x;
						ty = y + _y;
						break;
					case 1:
						tx = x + w - _x - 1;
						ty = y + h - _y - 1;
						break;
					case 2:
						tx = x + _y;
						ty = y + _x;
						break;
					case 3:
						tx = x + h - _y - 1;
						ty = y + w - _x - 1;
						break;
					case 4:
						tx = x + _x;
						ty = y + h - _y - 1;
						break;
					case 5:
						tx = x + h - _x - 1;
						ty = y + _y;
						break;
				}
				try {
					int l = img.getRGB(tx, ty) & 0xFFFFFF;
					if (bucket != null)
						bucket[C] = l;
					double[] transformed = activeScheme.translate(l);
					for(int K = 0; K < transformed.length; K++)
						out[c++] = transformed[K];
				} catch (Exception ex2) {
					// ...
					return -1;
				}
				C++;
			}
		}
		return c;
	}

	int findNearestSimilar(final BufferedImage image, final int x, final int y, final int blockSize, final int allocSize, final int blockCount) {
		final double block[] = new double[allocSize * activeScheme.channels()];
		transformBlock(image, x, y, blockSize, blockSize, block, null, 0);

		int closest = 0;
		boolean first = true;
		double maxSim = -2;
		double similarity = 0.0;

		for (int i = 0; i < blockCount; i++) {
			similarity = cosSimilarity(block, prototypes.get(i).data);
			if (Double.isNaN(similarity))
				continue;
			if (first || similarity > maxSim) {
				maxSim = similarity;
				closest = i;
				first = false;
			}
		}

		if(maxSim > -1) {
			prototypes.get(closest).hits++;
		}

		return closest;
	}

	void processImage(final BufferedImage image, final BufferedImage palette, final String outPath) throws IOException {
		final int realBlockSize = 1 << blockSize;
		final int pw = (palette.getWidth() >> blockSize) << blockSize;
		final int ph = (palette.getHeight() >> blockSize) << blockSize;
		final int iw = (image.getWidth() >> blockSize) << blockSize;
		final int ih = (image.getHeight() >> blockSize) << blockSize;
		final int cnt = realBlockSize * realBlockSize;
		final int blockCount = (pw * ph) / cnt;
		final int allocSize = cnt;
		final BufferedImage img = new BufferedImage(iw, ih, BufferedImage.TYPE_INT_RGB);		
		final List<Future<Integer>> results = new LinkedList<Future<Integer>>();

		if(prototypes == null) {
			prototypes = new ArrayList<>();
			buckets = new int[transforms * blockCount][cnt];

			for(int i=0; i<transforms * blockCount; i++) {
				prototypes.add(new Prototype(cnt, activeScheme.channels()));
			}

			final int aw = pw;
			final int ah = ph;
			final int step = realBlockSize;
			for (int y = 0; y < ah; y += step) {
				for (int x = 0; x < aw; x += step) {
					for (int i = 0; i < transforms; i++) {
						int res = transformBlock(palette, x, y, realBlockSize, realBlockSize, prototypes.get(blk).data, buckets[blk], i);
						if (res >= 0)
							blk++;
					}
				}
			}
		}

		int m = 0;
		for (int y = 0; y < ih; y += realBlockSize) {
			for (int x = 0; x < iw; x += realBlockSize, m++) {
				final int X = x;
				final int Y = y;
				results.add(executorService.submit( () -> findNearestSimilar(image, X, Y, realBlockSize, allocSize, blk) ));
			}
		}

		final List<Integer> resolved = results.parallelStream()
			.map(t -> {
				try {
					return t.get();
				} catch (InterruptedException | ExecutionException e) {
					return null;
				}
			})
			.toList();

		m = 0;
		for (int y = 0; y < ih; y += realBlockSize) {
			for (int x = 0; x < iw; x += realBlockSize, m++) {
				int n = 0;
				for (int j = 0; j < realBlockSize; j++) {
					for (int k = 0; k < realBlockSize; k++, n++) {
						int col = buckets[resolved.get(m)][n];
						img.setRGB(x + k, y + j, col);
					}
				}
			}
		}

		if (blend) {
			for (int y = 0; y < img.getHeight(); y++) {
				for (int x = 0; x < img.getWidth(); x++) {
					int ocol = image.getRGB(x, y);
					int or = (ocol >> 16) & 255;
					int og = (ocol >> 8) & 255;
					int ob = (ocol) & 255;
					or = or * or / 255;
					og = og * og / 255;
					ob = ob * ob / 255;
					ocol = (or << 16) | (og << 8) | ob;
					int ncol = img.getRGB(x, y);
					img.setRGB(x, y, mixColor(ncol, ocol));
				}
			}
		}

		String[] parts = outPath.split("\\.");
		ImageIO.write(img, parts[parts.length - 1], new File(outPath));
	}

	double cosSimilarity(final double[] arr1, final double[] arr2) {
		double A1A1 = 0.0, A2A2 = 0.0, A1A2 = 0.0;
		for(int i = 0; i < arr1.length; i++) {
			A1A1 += arr1[i] * arr1[i];
			A2A2 += arr2[i] * arr2[i];
			A1A2 += arr1[i] * arr2[i];
		}
		return A1A2 / Math.sqrt(A1A1 * A2A2);
	}

	static <T> boolean contains(final T[] array, final T v) {
		for (final T e : array)
			if (e == v || v != null && v.equals(e))
				return true;
		return false;
	}

	public static void main(String[] args) throws Exception {
		final Map<String, ColorScheme> schemes = Map.of(
			"rgb1", new RGB1(),
			"rgb3", new RGB3(),
			"rgb6", new RGB6(),
			"yuv1", new YUV1(),
			"yuv2", new YUV2()
		);
		final String[] oneArg = { 
			"blocks", "colors", "blur", "blend", 
			"heavy", "debug", "yuv", "bw",
			"plain" };

		final HashMap<String, String> params = new HashMap<String, String>();
		for (int i = 0; i < args.length; i++) {
			if (args[i].startsWith("--")) {
				String arg = args[i].substring(2);
				int len = 1;
				if (contains(oneArg, arg))
					len = 0;
				if ((i + len) < args.length)
					params.put(arg, args[i + len]);
				i += len;
			}
		}

		SR sr = SR.getInstance();
		if (params.containsKey("size"))
			sr.blockSize = Integer.parseInt(params.get("size"));
		if (params.containsKey("transforms"))
			sr.transforms = Integer.parseInt(params.get("transforms"));
		if (params.containsKey("noise"))
			sr.noise = Integer.parseInt(params.get("noise"));
		if (params.containsKey("blend"))
			sr.blend = true;
		if (params.containsKey("scheme"))
			sr.activeScheme = schemes.getOrDefault(params.get("scheme"), schemes.get("rgb6"));

		final String inPath = params.getOrDefault("in", "1.bmp");
		final String palettePath = params.getOrDefault("pattern", "2.bmp");
		final String outPath = params.getOrDefault("out", "3.bmp");
		BufferedImage palette = ImageIO.read(new File(palettePath));
		long start = System.currentTimeMillis();
		for(int i=1;;i++){
			String inImage = inPath.contains("%") ? String.format(inPath, i) : inPath;
			String outImage = outPath.contains("%") ? String.format(outPath, i) : outPath;
			if(!new File(inImage).exists()) break;
			BufferedImage image = ImageIO.read(new File(inImage));
			sr.processImage(image, palette, outImage);
			System.out.println("Processed frame " + i + ", (" + inImage + ", " + palettePath + ") => " + outImage + " (+" + ((System.currentTimeMillis() - start) / 1000.0) + ")");
			start = System.currentTimeMillis();
			if(!inPath.contains("%")) break;
		}
	}
};