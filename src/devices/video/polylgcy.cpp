// license:BSD-3-Clause
// copyright-holders:Ville Linde, Aaron Giles
/***************************************************************************

    polylgcy.c

    Legacy helper routines for polygon rendering.

***************************************************************************/

#include "emu.h"
#include "polylgcy.h"

#include <atomic>


namespace {

/***************************************************************************
    DEBUGGING
***************************************************************************/

/* keep statistics */
#define KEEP_STATISTICS                 0

/* turn this on to log the reasons for any long waits */
#define LOG_WAITS                       0

/* number of profiling ticks before we consider a wait "long" */
#define LOG_WAIT_THRESHOLD              1000



/***************************************************************************
    CONSTANTS
***************************************************************************/

#define SCANLINES_PER_BUCKET            8
#define CACHE_LINE_SIZE                 64          /* this is a general guess */
#define TOTAL_BUCKETS                   (512 / SCANLINES_PER_BUCKET)
#define UNITS_PER_POLY                  (100 / SCANLINES_PER_BUCKET)



/***************************************************************************
    TYPE DEFINITIONS
***************************************************************************/

/* forward definitions */
struct polygon_info;


/* tri_extent describes start/end points for a scanline */
struct tri_extent
{
	int16_t       startx;                     /* starting X coordinate (inclusive) */
	int16_t       stopx;                      /* ending X coordinate (exclusive) */
};


/* single set of polygon per-parameter data */
struct poly_param
{
	float       start;                      /* parameter value at starting X,Y */
	float       dpdx;                       /* dp/dx relative to starting X */
	float       dpdy;                       /* dp/dy relative to starting Y */
};


/* poly edge is used internally for quad rendering */
struct poly_edge
{
	poly_edge *         next;                   /* next edge in sequence */
	int                 index;                  /* index of this edge */
	const poly_vertex * v1;                     /* pointer to first vertex */
	const poly_vertex * v2;                     /* pointer to second vertex */
	float               dxdy;                   /* dx/dy along the edge */
	float               dpdy[POLYLGCY_MAX_VERTEX_PARAMS];/* per-parameter dp/dy values */
};


/* poly section is used internally for quad rendering */
struct poly_section
{
	const poly_edge *   ledge;                  /* pointer to left edge */
	const poly_edge *   redge;                  /* pointer to right edge */
	float               ybottom;                /* bottom of this section */
};


/* work_unit_shared is a common set of data shared between tris and quads */
struct work_unit_shared
{
	polygon_info *      polygon;                /* pointer to polygon */
	std::atomic<uint32_t> count_next;             /* number of scanlines and index of next item to process */
	int16_t               scanline;               /* starting scanline and count */
	uint16_t              previtem;               /* index of previous item in the same bucket */
#ifndef PTR64
	uint32_t              dummy;                  /* pad to 16 bytes */
#endif
};


/* tri_work_unit is a triangle-specific work-unit */
struct tri_work_unit
{
	work_unit_shared    shared;                 /* shared data */
	tri_extent          extent[SCANLINES_PER_BUCKET]; /* array of scanline extents */
};


/* quad_work_unit is a quad-specific work-unit */
struct quad_work_unit
{
	work_unit_shared    shared;                 /* shared data */
	poly_extent         extent[SCANLINES_PER_BUCKET]; /* array of scanline extents */
};


/* work_unit is a union of the two types */
union work_unit
{
	work_unit_shared    shared;                 /* shared data */
	tri_work_unit       tri;                    /* triangle work unit */
	quad_work_unit      quad;                   /* quad work unit */
};


/* polygon_info describes a single polygon, which includes the poly_params */
struct polygon_info
{
	legacy_poly_manager *      poly;                   /* pointer back to the poly manager */
	void *              dest;                   /* pointer to the destination we are rendering to */
	void *              extra;                  /* extra data pointer */
	uint8_t               numparams;              /* number of parameters for this polygon  */
	uint8_t               numverts;               /* number of vertices in this polygon */
	poly_draw_scanline_func     callback;               /* callback to handle a scanline's worth of work */
	int32_t               xorigin;                /* X origin for all parameters */
	int32_t               yorigin;                /* Y origin for all parameters */
	poly_param          param[POLYLGCY_MAX_VERTEX_PARAMS];/* array of parameter data */
};

} // anonymous namespace


/* full poly manager description */
struct legacy_poly_manager
{
	/* queue management */
	osd_work_queue *    queue;                  /* work queue */

	/* triangle work units */
	std::vector<work_unit *> unit;                   /* array of work unit pointers */
	uint32_t              unit_next;              /* index of next unit to allocate */
	uint32_t              unit_count;             /* number of work units available */
	size_t              unit_size;              /* size of each work unit, in bytes */

	/* quad work units */
	uint32_t              quadunit_next;          /* index of next unit to allocate */
	uint32_t              quadunit_count;         /* number of work units available */
	size_t              quadunit_size;          /* size of each work unit, in bytes */

	/* poly data */
	std::vector<polygon_info *> polygon;                /* array of polygon pointers */
	uint32_t              polygon_next;           /* index of next polygon to allocate */
	uint32_t              polygon_count;          /* number of polygon items available */
	size_t              polygon_size;           /* size of each polygon, in bytes */

	/* extra data */
	std::vector<void *> extra;                  /* array of extra data pointers */
	uint32_t              extra_next;             /* index of next extra data to allocate */
	uint32_t              extra_count;            /* number of extra data items available */
	size_t              extra_size;             /* size of each extra data, in bytes */

	/* misc data */
	uint8_t               flags;                  /* flags */

	/* buckets */
	uint16_t              unit_bucket[TOTAL_BUCKETS]; /* buckets for tracking unit usage */

	/* statistics */
	uint32_t              triangles;              /* number of triangles queued */
	uint32_t              quads;                  /* number of quads queued */
	uint64_t              pixels;                 /* number of pixels rendered */
#if KEEP_STATISTICS
	uint32_t              unit_waits;             /* number of times we waited for a unit */
	uint32_t              unit_max;               /* maximum units used */
	uint32_t              polygon_waits;          /* number of times we waited for a polygon */
	uint32_t              polygon_max;            /* maximum polygons used */
	uint32_t              extra_waits;            /* number of times we waited for an extra data */
	uint32_t              extra_max;              /* maximum extra data used */
	uint32_t              conflicts[WORK_MAX_THREADS]; /* number of conflicts found, per thread */
	uint32_t              resolved[WORK_MAX_THREADS]; /* number of conflicts resolved, per thread */
#endif

	std::vector<uint8_t> m_work_unit_alloc;
	std::vector<uint8_t> m_polygon_alloc;
	std::vector<uint8_t> m_extra_alloc;
};



/***************************************************************************
    FUNCTION PROTOTYPES
***************************************************************************/

static void *poly_item_callback(void *param, int threadid);
static void poly_state_presave(legacy_poly_manager &poly);



/***************************************************************************
    INLINE FUNCTIONS
***************************************************************************/

/*-------------------------------------------------
    round_coordinate - round a coordinate to
    an integer, following rules that 0.5 rounds
    down
-------------------------------------------------*/

static inline int32_t round_coordinate(float value)
{
	int32_t result = floor(value);
	return result + (value - (float)result > 0.5f);
}


/*-------------------------------------------------
    convert_tri_extent_to_poly_extent - convert
    a simple tri_extent to a full poly_extent
-------------------------------------------------*/

static inline void convert_tri_extent_to_poly_extent(poly_extent *dstextent, const tri_extent *srcextent, const polygon_info *polygon, int32_t y)
{
	/* copy start/stop always */
	dstextent->startx = srcextent->startx;
	dstextent->stopx = srcextent->stopx;

	/* if we have parameters, process them as well */
	for (int paramnum = 0; paramnum < polygon->numparams; paramnum++)
	{
		dstextent->param[paramnum].start = polygon->param[paramnum].start + srcextent->startx * polygon->param[paramnum].dpdx + y * polygon->param[paramnum].dpdy;
		dstextent->param[paramnum].dpdx = polygon->param[paramnum].dpdx;
	}
}


/*-------------------------------------------------
    interpolate_vertex - interpolate values in
    a vertex based on p[0] crossing the clipval
-------------------------------------------------*/

static inline void interpolate_vertex(poly_vertex *outv, const poly_vertex *v1, const poly_vertex *v2, int paramcount, float clipval)
{
	float frac = (clipval - v1->p[0]) / (v2->p[0] - v1->p[0]);
	int paramnum;

	/* create a new one at the intersection point */
	outv->x = v1->x + frac * (v2->x - v1->x);
	outv->y = v1->y + frac * (v2->y - v1->y);
	for (paramnum = 0; paramnum < paramcount; paramnum++)
		outv->p[paramnum] = v1->p[paramnum] + frac * (v2->p[paramnum] - v1->p[paramnum]);
}


/*-------------------------------------------------
    copy_vertex - copy vertex data from one to
    another
-------------------------------------------------*/

static inline void copy_vertex(poly_vertex *outv, const poly_vertex *v, int paramcount)
{
	int paramnum;

	outv->x = v->x;
	outv->y = v->y;
	for (paramnum = 0; paramnum < paramcount; paramnum++)
		outv->p[paramnum] = v->p[paramnum];
}


/*-------------------------------------------------
    allocate_polygon - allocate a new polygon
    object, blocking if we run out
-------------------------------------------------*/

static inline polygon_info *allocate_polygon(legacy_poly_manager *poly, int miny, int maxy)
{
	/* wait for a work item if we have to */
	if (poly->polygon_next + 1 > poly->polygon_count)
	{
		poly_wait(poly, "Out of polygons");
#if KEEP_STATISTICS
		poly->polygon_waits++;
#endif
	}
	else if (poly->unit_next + (maxy - miny) / SCANLINES_PER_BUCKET + 2 > poly->unit_count)
	{
		poly_wait(poly, "Out of work units");
#if KEEP_STATISTICS
		poly->unit_waits++;
#endif
	}
#if KEEP_STATISTICS
	poly->polygon_max = std::max(poly->polygon_max, poly->polygon_next + 1);
#endif
	return poly->polygon[poly->polygon_next++];
}



/***************************************************************************
    INITIALIZATION/TEARDOWN
***************************************************************************/

legacy_poly_manager_owner::legacy_poly_manager_owner() : m_poly(nullptr)
{
}


legacy_poly_manager_owner::~legacy_poly_manager_owner()
{
	delete m_poly;
}

/*-------------------------------------------------
    allocate_array - allocate an array of pointers
-------------------------------------------------*/

template<typename T>
static void allocate_array(running_machine &machine, std::vector<uint8_t> &buffer, std::vector<T *> &ptrarray, size_t *itemsize, uint32_t itemcount)
{
	int itemnum;

	/* fail if 0 */
	if (itemcount == 0)
		return;

	/* round to a cache line boundary */
	*itemsize = ((*itemsize + CACHE_LINE_SIZE - 1) / CACHE_LINE_SIZE) * CACHE_LINE_SIZE;

	/* allocate the array */
	ptrarray.resize(itemcount);

	/* allocate the actual items */
	buffer.resize(*itemsize * itemcount);

	/* initialize the pointer array */
	for (itemnum = 0; itemnum < itemcount; itemnum++)
		ptrarray[itemnum] = reinterpret_cast<T *>(&buffer[*itemsize * itemnum]);
}



/*-------------------------------------------------
    poly_alloc - initialize a new polygon
    manager
-------------------------------------------------*/

void poly_alloc(legacy_poly_manager_owner &owner, running_machine &machine, int max_polys, size_t extra_data_size, uint8_t flags)
{
	/* allocate the manager itself */
	legacy_poly_manager *poly = new legacy_poly_manager;
	owner.m_poly = poly;
	poly->flags = flags;

	/* allocate polygons */
	poly->polygon_size = sizeof(polygon_info);
	poly->polygon_count = std::max(max_polys, 1);
	poly->polygon_next = 0;
	allocate_array(machine, poly->m_polygon_alloc, poly->polygon, &poly->polygon_size, poly->polygon_count);

	/* allocate extra data */
	poly->extra_size = extra_data_size;
	poly->extra_count = poly->polygon_count;
	poly->extra_next = 1;
	allocate_array(machine, poly->m_extra_alloc, poly->extra, &poly->extra_size, poly->extra_count);

	/* allocate triangle work units */
	poly->unit_size = (flags & POLYLGCY_FLAG_ALLOW_QUADS) ? sizeof(quad_work_unit) : sizeof(tri_work_unit);
	poly->unit_count = std::min(poly->polygon_count * UNITS_PER_POLY, 65535U);
	poly->unit_next = 0;
	allocate_array(machine, poly->m_work_unit_alloc, poly->unit, &poly->unit_size, poly->unit_count);

	/* create the work queue */
	if (!(flags & POLYLGCY_FLAG_NO_WORK_QUEUE))
		poly->queue = osd_work_queue_alloc(WORK_QUEUE_FLAG_MULTI | WORK_QUEUE_FLAG_HIGH_FREQ);

	/* request a pre-save callback for synchronization */
	machine.save().register_presave(save_prepost_delegate(FUNC(poly_state_presave), poly));
}


/*-------------------------------------------------
    poly_free - free a polygon manager
-------------------------------------------------*/

void poly_free(legacy_poly_manager *poly)
{
#if KEEP_STATISTICS
{
	int conflicts = 0, resolved = 0;
	for (int i = 0; i < std::size(poly->conflicts); i++)
	{
		conflicts += poly->conflicts[i];
		resolved += poly->resolved[i];
	}
	printf("Total triangles = %d\n", poly->triangles);
	printf("Total quads = %d\n", poly->quads);
	if (poly->pixels > 1000000000)
		printf("Total pixels   = %d%09d\n", (uint32_t)(poly->pixels / 1000000000), (uint32_t)(poly->pixels % 1000000000));
	else
		printf("Total pixels   = %d\n", (uint32_t)poly->pixels);
	printf("Conflicts:  %d resolved, %d total\n", resolved, conflicts);
	printf("Units:      %5d used, %5d allocated, %5d waits, %4d bytes each, %7d total\n", poly->unit_max, poly->unit_count, poly->unit_waits, (u32) poly->unit_size, poly->unit_count * (u32) poly->unit_size);
	printf("Polygons:   %5d used, %5d allocated, %5d waits, %4d bytes each, %7d total\n", poly->polygon_max, poly->polygon_count, poly->polygon_waits, (u32) poly->polygon_size, poly->polygon_count * (u32) poly->polygon_size);
	printf("Extra data: %5d used, %5d allocated, %5d waits, %4d bytes each, %7d total\n", poly->extra_max, poly->extra_count, poly->extra_waits, (u32) poly->extra_size, poly->extra_count * (u32) poly->extra_size);
}
#endif

	/* free the work queue */
	if (poly->queue != nullptr)
		osd_work_queue_free(poly->queue);
}



/***************************************************************************
    COMMON FUNCTIONS
***************************************************************************/

/*-------------------------------------------------
    poly_wait - wait for all pending rendering
    to complete
-------------------------------------------------*/

void poly_wait(legacy_poly_manager *poly, const char *debug_reason)
{
	osd_ticks_t time;

	/* remember the start time if we're logging */
	if (LOG_WAITS)
		time = get_profile_ticks();

	/* wait for all pending work items to complete */
	if (poly->queue != nullptr)
		osd_work_queue_wait(poly->queue, osd_ticks_per_second() * 100);

	/* if we don't have a queue, just run the whole list now */
	else
	{
		int unitnum;
		for (unitnum = 0; unitnum < poly->unit_next; unitnum++)
			poly_item_callback(poly->unit[unitnum], 0);
	}

	/* log any long waits */
	if (LOG_WAITS)
	{
		time = get_profile_ticks() - time;
		if (time > LOG_WAIT_THRESHOLD)
			osd_printf_verbose("Poly:Waited %d cycles for %s\n", (int)time, debug_reason);
	}

	/* reset the state */
	poly->polygon_next = poly->unit_next = 0;
	memset(poly->unit_bucket, 0xff, sizeof(poly->unit_bucket));

	/* we need to preserve the last extra data that was supplied */
	if (poly->extra_next > 1)
		memcpy(poly->extra[0], poly->extra[poly->extra_next - 1], poly->extra_size);
	poly->extra_next = 1;
}


/*-------------------------------------------------
    poly_get_extra_data - get a pointer to the
    extra data for the next polygon
-------------------------------------------------*/

void *poly_get_extra_data(legacy_poly_manager *poly)
{
	/* wait for a work item if we have to */
	if (poly->extra_next + 1 > poly->extra_count)
	{
		poly_wait(poly, "Out of extra data");
#if KEEP_STATISTICS
		poly->extra_waits++;
#endif
	}

	/* return a pointer to the extra data for the next item */
#if KEEP_STATISTICS
	poly->extra_max = std::max(poly->extra_max, poly->extra_next + 1);
#endif
	return poly->extra[poly->extra_next++];
}



/***************************************************************************
    CORE TRIANGLE RENDERING
***************************************************************************/

/*-------------------------------------------------
    poly_render_triangle - render a single
    triangle given 3 vertexes
-------------------------------------------------*/

uint32_t poly_render_triangle(legacy_poly_manager *poly, void *dest, const rectangle &cliprect, poly_draw_scanline_func callback, int paramcount, const poly_vertex *v1, const poly_vertex *v2, const poly_vertex *v3)
{
	float dxdy_v1v2, dxdy_v1v3, dxdy_v2v3;
	const poly_vertex *tv;
	int32_t curscan, scaninc;
	polygon_info *polygon;
	int32_t v1yclip, v3yclip;
	int32_t v1y, v3y, v1x;
	int32_t pixels = 0;
	uint32_t startunit;

	/* first sort by Y */
	if (v2->y < v1->y)
	{
		tv = v1;
		v1 = v2;
		v2 = tv;
	}
	if (v3->y < v2->y)
	{
		tv = v2;
		v2 = v3;
		v3 = tv;
		if (v2->y < v1->y)
		{
			tv = v1;
			v1 = v2;
			v2 = tv;
		}
	}

	/* compute some integral X/Y vertex values */
	v1x = round_coordinate(v1->x);
	v1y = round_coordinate(v1->y);
	v3y = round_coordinate(v3->y);

	/* clip coordinates */
	v1yclip = v1y;
	v3yclip = v3y + ((poly->flags & POLYLGCY_FLAG_INCLUDE_BOTTOM_EDGE) ? 1 : 0);
	v1yclip = std::max(v1yclip, cliprect.min_y);
	v3yclip = std::min(v3yclip, cliprect.max_y + 1);
	if (v3yclip - v1yclip <= 0)
		return 0;

	/* allocate a new polygon */
	polygon = allocate_polygon(poly, v1yclip, v3yclip);

	/* fill in the polygon information */
	polygon->poly = poly;
	polygon->dest = dest;
	polygon->callback = callback;
	polygon->extra = poly->extra[poly->extra_next - 1];
	polygon->numparams = paramcount;
	polygon->numverts = 3;

	/* set the start X/Y coordinates */
	polygon->xorigin = v1x;
	polygon->yorigin = v1y;

	/* compute the slopes for each portion of the triangle */
	dxdy_v1v2 = (v2->y == v1->y) ? 0.0f : (v2->x - v1->x) / (v2->y - v1->y);
	dxdy_v1v3 = (v3->y == v1->y) ? 0.0f : (v3->x - v1->x) / (v3->y - v1->y);
	dxdy_v2v3 = (v3->y == v2->y) ? 0.0f : (v3->x - v2->x) / (v3->y - v2->y);

	/* compute the X extents for each scanline */
	startunit = poly->unit_next;
	for (curscan = v1yclip; curscan < v3yclip; curscan += scaninc)
	{
		uint32_t bucketnum = ((uint32_t)curscan / SCANLINES_PER_BUCKET) % TOTAL_BUCKETS;
		uint32_t unit_index = poly->unit_next++;
		tri_work_unit *unit = &poly->unit[unit_index]->tri;
		int extnum;

		/* determine how much to advance to hit the next bucket */
		scaninc = SCANLINES_PER_BUCKET - (uint32_t)curscan % SCANLINES_PER_BUCKET;

		/* fill in the work unit basics */
		unit->shared.polygon = polygon;
		unit->shared.count_next = std::min(v3yclip - curscan, scaninc);
		unit->shared.scanline = curscan;
		unit->shared.previtem = poly->unit_bucket[bucketnum];
		poly->unit_bucket[bucketnum] = unit_index;

		/* iterate over extents */
		for (extnum = 0; extnum < unit->shared.count_next; extnum++)
		{
			float fully = (float)(curscan + extnum) + 0.5f;
			float startx = v1->x + (fully - v1->y) * dxdy_v1v3;
			float stopx;
			int32_t istartx, istopx;

			/* compute the ending X based on which part of the triangle we're in */
			if (fully < v2->y)
				stopx = v1->x + (fully - v1->y) * dxdy_v1v2;
			else
				stopx = v2->x + (fully - v2->y) * dxdy_v2v3;

			/* clamp to full pixels */
			istartx = round_coordinate(startx);
			istopx = round_coordinate(stopx);

			/* force start < stop */
			if (istartx > istopx)
			{
				int32_t temp = istartx;
				istartx = istopx;
				istopx = temp;
			}

			/* include the right edge if requested */
			if (poly->flags & POLYLGCY_FLAG_INCLUDE_RIGHT_EDGE)
				istopx++;

			/* apply left/right clipping */
			if (istartx < cliprect.min_x)
				istartx = cliprect.min_x;
			if (istopx > cliprect.max_x)
				istopx = cliprect.max_x + 1;

			/* set the extent and update the total pixel count */
			if (istartx >= istopx)
				istartx = istopx = 0;
			unit->extent[extnum].startx = istartx;
			unit->extent[extnum].stopx = istopx;
			pixels += istopx - istartx;
		}
	}
#if KEEP_STATISTICS
	poly->unit_max = std::max(poly->unit_max, poly->unit_next);
#endif

	/* compute parameter starting points and deltas */
	if (paramcount > 0)
	{
		float a00 = v2->y - v3->y;
		float a01 = v3->x - v2->x;
		float a02 = v2->x*v3->y - v3->x*v2->y;
		float a10 = v3->y - v1->y;
		float a11 = v1->x - v3->x;
		float a12 = v3->x*v1->y - v1->x*v3->y;
		float a20 = v1->y - v2->y;
		float a21 = v2->x - v1->x;
		float a22 = v1->x*v2->y - v2->x*v1->y;
		float det = a02 + a12 + a22;

		if(fabsf(det) < 0.001f) {
			for (int paramnum = 0; paramnum < paramcount; paramnum++)
			{
				poly_param *params = &polygon->param[paramnum];
				params->dpdx = 0;
				params->dpdy = 0;
				params->start = v1->p[paramnum];
			}
		}
		else
		{
			float idet = 1/det;
			for (int paramnum = 0; paramnum < paramcount; paramnum++)
			{
				poly_param *params = &polygon->param[paramnum];
				params->dpdx  = idet*(v1->p[paramnum]*a00 + v2->p[paramnum]*a10 + v3->p[paramnum]*a20);
				params->dpdy  = idet*(v1->p[paramnum]*a01 + v2->p[paramnum]*a11 + v3->p[paramnum]*a21);
				params->start = idet*(v1->p[paramnum]*a02 + v2->p[paramnum]*a12 + v3->p[paramnum]*a22);
			}
		}
	}

	/* enqueue the work items */
	if (poly->queue != nullptr)
		osd_work_item_queue_multiple(poly->queue, poly_item_callback, poly->unit_next - startunit, poly->unit[startunit], poly->unit_size, WORK_ITEM_FLAG_AUTO_RELEASE);

	/* return the total number of pixels in the triangle */
	poly->triangles++;
	poly->pixels += pixels;
	return pixels;
}


/*-------------------------------------------------
    poly_render_triangle_fan - render a set of
    triangles in a fan
-------------------------------------------------*/

uint32_t poly_render_triangle_fan(legacy_poly_manager *poly, void *dest, const rectangle &cliprect, poly_draw_scanline_func callback, int paramcount, int numverts, const poly_vertex *v)
{
	uint32_t pixels = 0;
	int vertnum;

	/* iterate over vertices */
	for (vertnum = 2; vertnum < numverts; vertnum++)
		pixels += poly_render_triangle(poly, dest, cliprect, callback, paramcount, &v[0], &v[vertnum - 1], &v[vertnum]);
	return pixels;
}


/*-------------------------------------------------
    poly_render_triangle_custom - perform a custom
    render of an object, given specific extents
-------------------------------------------------*/

uint32_t poly_render_triangle_custom(legacy_poly_manager *poly, void *dest, const rectangle &cliprect, poly_draw_scanline_func callback, int startscanline, int numscanlines, const poly_extent *extents)
{
	int32_t curscan, scaninc;
	polygon_info *polygon;
	int32_t v1yclip, v3yclip;
	int32_t pixels = 0;
	uint32_t startunit;

	/* clip coordinates */
	v1yclip = std::max(startscanline, cliprect.min_y);
	v3yclip = std::min(startscanline + numscanlines, cliprect.max_y + 1);
	if (v3yclip - v1yclip <= 0)
		return 0;

	/* allocate a new polygon */
	polygon = allocate_polygon(poly, v1yclip, v3yclip);

	/* fill in the polygon information */
	polygon->poly = poly;
	polygon->dest = dest;
	polygon->callback = callback;
	polygon->extra = poly->extra[poly->extra_next - 1];
	polygon->numparams = 0;
	polygon->numverts = 3;

	/* compute the X extents for each scanline */
	startunit = poly->unit_next;
	for (curscan = v1yclip; curscan < v3yclip; curscan += scaninc)
	{
		uint32_t bucketnum = ((uint32_t)curscan / SCANLINES_PER_BUCKET) % TOTAL_BUCKETS;
		uint32_t unit_index = poly->unit_next++;
		tri_work_unit *unit = &poly->unit[unit_index]->tri;
		int extnum;

		/* determine how much to advance to hit the next bucket */
		scaninc = SCANLINES_PER_BUCKET - (uint32_t)curscan % SCANLINES_PER_BUCKET;

		/* fill in the work unit basics */
		unit->shared.polygon = polygon;
		unit->shared.count_next = std::min(v3yclip - curscan, scaninc);
		unit->shared.scanline = curscan;
		unit->shared.previtem = poly->unit_bucket[bucketnum];
		poly->unit_bucket[bucketnum] = unit_index;

		/* iterate over extents */
		for (extnum = 0; extnum < unit->shared.count_next; extnum++)
		{
			const poly_extent *extent = &extents[(curscan + extnum) - startscanline];
			int32_t istartx = extent->startx, istopx = extent->stopx;

			/* force start < stop */
			if (istartx > istopx)
			{
				int32_t temp = istartx;
				istartx = istopx;
				istopx = temp;
			}

			/* apply left/right clipping */
			if (istartx < cliprect.min_x)
				istartx = cliprect.min_x;
			if (istopx > cliprect.max_x)
				istopx = cliprect.max_x + 1;

			/* set the extent and update the total pixel count */
			unit->extent[extnum].startx = istartx;
			unit->extent[extnum].stopx = istopx;
			if (istartx < istopx)
				pixels += istopx - istartx;
		}
	}
#if KEEP_STATISTICS
	poly->unit_max = std::max(poly->unit_max, poly->unit_next);
#endif

	/* enqueue the work items */
	if (poly->queue != nullptr)
		osd_work_item_queue_multiple(poly->queue, poly_item_callback, poly->unit_next - startunit, poly->unit[startunit], poly->unit_size, WORK_ITEM_FLAG_AUTO_RELEASE);

	/* return the total number of pixels in the object */
	poly->triangles++;
	poly->pixels += pixels;
	return pixels;
}



/***************************************************************************
    CORE QUAD RENDERING
***************************************************************************/

/*-------------------------------------------------
    poly_render_quad - render a single quad
    given 4 vertexes
-------------------------------------------------*/

uint32_t poly_render_quad(legacy_poly_manager *poly, void *dest, const rectangle &cliprect, poly_draw_scanline_func callback, int paramcount, const poly_vertex *v1, const poly_vertex *v2, const poly_vertex *v3, const poly_vertex *v4)
{
	poly_edge fedgelist[3], bedgelist[3];
	const poly_edge *ledge, *redge;
	const poly_vertex *v[4];
	poly_edge *edgeptr;
	int minv, maxv, curv;
	int32_t minyclip, maxyclip;
	int32_t miny, maxy;
	int32_t curscan, scaninc;
	polygon_info *polygon;
	int32_t pixels = 0;
	uint32_t startunit;

	assert(poly->flags & POLYLGCY_FLAG_ALLOW_QUADS);

	/* arrays make things easier */
	v[0] = v1;
	v[1] = v2;
	v[2] = v3;
	v[3] = v4;

	/* determine min/max Y vertices */
	if (v[1]->y < v[0]->y)
		minv = 1, maxv = 0;
	else
		minv = 0, maxv = 1;
	if (v[2]->y < v[minv]->y)
		minv = 2;
	else if (v[2]->y > v[maxv]->y)
		maxv = 2;
	if (v[3]->y < v[minv]->y)
		minv = 3;
	else if (v[3]->y > v[maxv]->y)
		maxv = 3;

	/* determine start/end scanlines */
	miny = round_coordinate(v[minv]->y);
	maxy = round_coordinate(v[maxv]->y);

	/* clip coordinates */
	minyclip = miny;
	maxyclip = maxy + ((poly->flags & POLYLGCY_FLAG_INCLUDE_BOTTOM_EDGE) ? 1 : 0);
	minyclip = std::max(minyclip, cliprect.min_y);
	maxyclip = std::min(maxyclip, cliprect.max_y + 1);
	if (maxyclip - minyclip <= 0)
		return 0;

	/* allocate a new polygon */
	polygon = allocate_polygon(poly, minyclip, maxyclip);

	/* fill in the polygon information */
	polygon->poly = poly;
	polygon->dest = dest;
	polygon->callback = callback;
	polygon->extra = poly->extra[poly->extra_next - 1];
	polygon->numparams = paramcount;
	polygon->numverts = 4;

	/* walk forward to build up the forward edge list */
	edgeptr = &fedgelist[0];
	for (curv = minv; curv != maxv; curv = (curv + 1) & 3)
	{
		int paramnum;
		float ooy;

		/* set the two vertices */
		edgeptr->v1 = v[curv];
		edgeptr->v2 = v[(curv + 1) & 3];

		/* if horizontal, skip altogether */
		if (edgeptr->v1->y == edgeptr->v2->y)
			continue;

		/* need dx/dy always, and parameter deltas as necessary */
		ooy = 1.0f / (edgeptr->v2->y - edgeptr->v1->y);
		edgeptr->dxdy = (edgeptr->v2->x - edgeptr->v1->x) * ooy;
		for (paramnum = 0; paramnum < paramcount; paramnum++)
			edgeptr->dpdy[paramnum] = (edgeptr->v2->p[paramnum] - edgeptr->v1->p[paramnum]) * ooy;
		edgeptr++;
	}

	/* walk backward to build up the backward edge list */
	edgeptr = &bedgelist[0];
	for (curv = minv; curv != maxv; curv = (curv - 1) & 3)
	{
		int paramnum;
		float ooy;

		/* set the two vertices */
		edgeptr->v1 = v[curv];
		edgeptr->v2 = v[(curv - 1) & 3];

		/* if horizontal, skip altogether */
		if (edgeptr->v1->y == edgeptr->v2->y)
			continue;

		/* need dx/dy always, and parameter deltas as necessary */
		ooy = 1.0f / (edgeptr->v2->y - edgeptr->v1->y);
		edgeptr->dxdy = (edgeptr->v2->x - edgeptr->v1->x) * ooy;
		for (paramnum = 0; paramnum < paramcount; paramnum++)
			edgeptr->dpdy[paramnum] = (edgeptr->v2->p[paramnum] - edgeptr->v1->p[paramnum]) * ooy;
		edgeptr++;
	}

	/* determine which list is left/right: */
	/* if the first vertex is shared, compare the slopes */
	/* if the first vertex is not shared, compare the X coordinates */
	if ((fedgelist[0].v1 == bedgelist[0].v1 && fedgelist[0].dxdy < bedgelist[0].dxdy) ||
		(fedgelist[0].v1 != bedgelist[0].v1 && fedgelist[0].v1->x < bedgelist[0].v1->x))
	{
		ledge = fedgelist;
		redge = bedgelist;
	}
	else
	{
		ledge = bedgelist;
		redge = fedgelist;
	}

	/* compute the X extents for each scanline */
	startunit = poly->unit_next;
	for (curscan = minyclip; curscan < maxyclip; curscan += scaninc)
	{
		uint32_t bucketnum = ((uint32_t)curscan / SCANLINES_PER_BUCKET) % TOTAL_BUCKETS;
		uint32_t unit_index = poly->unit_next++;
		quad_work_unit *unit = &poly->unit[unit_index]->quad;
		int extnum;

		/* determine how much to advance to hit the next bucket */
		scaninc = SCANLINES_PER_BUCKET - (uint32_t)curscan % SCANLINES_PER_BUCKET;

		/* fill in the work unit basics */
		unit->shared.polygon = polygon;
		unit->shared.count_next = std::min(maxyclip - curscan, scaninc);
		unit->shared.scanline = curscan;
		unit->shared.previtem = poly->unit_bucket[bucketnum];
		poly->unit_bucket[bucketnum] = unit_index;

		/* iterate over extents */
		for (extnum = 0; extnum < unit->shared.count_next; extnum++)
		{
			float fully = (float)(curscan + extnum) + 0.5f;
			float startx, stopx;
			int32_t istartx, istopx;
			int paramnum;

			/* compute the ending X based on which part of the triangle we're in */
			while (fully > ledge->v2->y && fully < v[maxv]->y)
				ledge++;
			while (fully > redge->v2->y && fully < v[maxv]->y)
				redge++;
			startx = ledge->v1->x + (fully - ledge->v1->y) * ledge->dxdy;
			stopx = redge->v1->x + (fully - redge->v1->y) * redge->dxdy;

			/* clamp to full pixels */
			istartx = round_coordinate(startx);
			istopx = round_coordinate(stopx);

			/* compute parameter starting points and deltas */
			if (paramcount > 0)
			{
				float ldy = fully - ledge->v1->y;
				float rdy = fully - redge->v1->y;
				float oox = 1.0f / (stopx - startx);

				/* iterate over parameters */
				for (paramnum = 0; paramnum < paramcount; paramnum++)
				{
					float lparam = ledge->v1->p[paramnum] + ldy * ledge->dpdy[paramnum];
					float rparam = redge->v1->p[paramnum] + rdy * redge->dpdy[paramnum];
					float dpdx = (rparam - lparam) * oox;

					unit->extent[extnum].param[paramnum].start = lparam;// - ((float)istartx + 0.5f) * dpdx;
					unit->extent[extnum].param[paramnum].dpdx = dpdx;
				}
			}

			/* include the right edge if requested */
			if (poly->flags & POLYLGCY_FLAG_INCLUDE_RIGHT_EDGE)
				istopx++;

			/* apply left/right clipping */
			if (istartx < cliprect.min_x)
			{
				for (paramnum = 0; paramnum < paramcount; paramnum++)
					unit->extent[extnum].param[paramnum].start += (cliprect.min_x - istartx) * unit->extent[extnum].param[paramnum].dpdx;
				istartx = cliprect.min_x;
			}
			if (istopx > cliprect.max_x)
				istopx = cliprect.max_x + 1;

			/* set the extent and update the total pixel count */
			if (istartx >= istopx)
				istartx = istopx = 0;
			unit->extent[extnum].startx = istartx;
			unit->extent[extnum].stopx = istopx;
			pixels += istopx - istartx;
		}
	}
#if KEEP_STATISTICS
	poly->unit_max = std::max(poly->unit_max, poly->unit_next);
#endif

	/* enqueue the work items */
	if (poly->queue != nullptr)
		osd_work_item_queue_multiple(poly->queue, poly_item_callback, poly->unit_next - startunit, poly->unit[startunit], poly->unit_size, WORK_ITEM_FLAG_AUTO_RELEASE);

	/* return the total number of pixels in the triangle */
	poly->quads++;
	poly->pixels += pixels;
	return pixels;
}


/*-------------------------------------------------
    poly_render_quad_fan - render a set of
    quads in a fan
-------------------------------------------------*/

uint32_t poly_render_quad_fan(legacy_poly_manager *poly, void *dest, const rectangle &cliprect, poly_draw_scanline_func callback, int paramcount, int numverts, const poly_vertex *v)
{
	uint32_t pixels = 0;
	int vertnum;

	/* iterate over vertices */
	for (vertnum = 2; vertnum < numverts; vertnum += 2)
		pixels += poly_render_quad(poly, dest, cliprect, callback, paramcount, &v[0], &v[vertnum - 1], &v[vertnum], &v[std::min(vertnum + 1, numverts - 1)]);
	return pixels;
}



/***************************************************************************
    CORE POLYGON RENDERING
***************************************************************************/

/*-------------------------------------------------
    poly_render_polygon - render a single polygon up
    to 32 vertices
-------------------------------------------------*/

uint32_t poly_render_polygon(legacy_poly_manager *poly, void *dest, const rectangle &cliprect, poly_draw_scanline_func callback, int paramcount, int numverts, const poly_vertex *v)
{
	poly_edge fedgelist[POLYLGCY_MAX_POLYGON_VERTS - 1], bedgelist[POLYLGCY_MAX_POLYGON_VERTS - 1];
	const poly_edge *ledge, *redge;
	poly_edge *edgeptr;
	int minv, maxv, curv;
	int32_t minyclip, maxyclip;
	int32_t miny, maxy;
	int32_t curscan, scaninc;
	polygon_info *polygon;
	int32_t pixels = 0;
	uint32_t startunit;
	int vertnum;

	assert(poly->flags & POLYLGCY_FLAG_ALLOW_QUADS);

	/* determine min/max Y vertices */
	minv = maxv = 0;
	for (vertnum = 1; vertnum < numverts; vertnum++)
	{
		if (v[vertnum].y < v[minv].y)
			minv = vertnum;
		else if (v[vertnum].y > v[maxv].y)
			maxv = vertnum;
	}

	/* determine start/end scanlines */
	miny = round_coordinate(v[minv].y);
	maxy = round_coordinate(v[maxv].y);

	/* clip coordinates */
	minyclip = miny;
	maxyclip = maxy + ((poly->flags & POLYLGCY_FLAG_INCLUDE_BOTTOM_EDGE) ? 1 : 0);
	minyclip = std::max(minyclip, cliprect.min_y);
	maxyclip = std::min(maxyclip, cliprect.max_y + 1);
	if (maxyclip - minyclip <= 0)
		return 0;

	/* allocate a new polygon */
	polygon = allocate_polygon(poly, minyclip, maxyclip);

	/* fill in the polygon information */
	polygon->poly = poly;
	polygon->dest = dest;
	polygon->callback = callback;
	polygon->extra = poly->extra[poly->extra_next - 1];
	polygon->numparams = paramcount;
	polygon->numverts = numverts;

	/* walk forward to build up the forward edge list */
	edgeptr = &fedgelist[0];
	for (curv = minv; curv != maxv; curv = (curv == numverts - 1) ? 0 : (curv + 1))
	{
		int paramnum;
		float ooy;

		/* set the two vertices */
		edgeptr->v1 = &v[curv];
		edgeptr->v2 = &v[(curv == numverts - 1) ? 0 : (curv + 1)];

		/* if horizontal, skip altogether */
		if (edgeptr->v1->y == edgeptr->v2->y)
			continue;

		/* need dx/dy always, and parameter deltas as necessary */
		ooy = 1.0f / (edgeptr->v2->y - edgeptr->v1->y);
		edgeptr->dxdy = (edgeptr->v2->x - edgeptr->v1->x) * ooy;
		for (paramnum = 0; paramnum < paramcount; paramnum++)
			edgeptr->dpdy[paramnum] = (edgeptr->v2->p[paramnum] - edgeptr->v1->p[paramnum]) * ooy;
		edgeptr++;
	}

	/* walk backward to build up the backward edge list */
	edgeptr = &bedgelist[0];
	for (curv = minv; curv != maxv; curv = (curv == 0) ? (numverts - 1) : (curv - 1))
	{
		int paramnum;
		float ooy;

		/* set the two vertices */
		edgeptr->v1 = &v[curv];
		edgeptr->v2 = &v[(curv == 0) ? (numverts - 1) : (curv - 1)];

		/* if horizontal, skip altogether */
		if (edgeptr->v1->y == edgeptr->v2->y)
			continue;

		/* need dx/dy always, and parameter deltas as necessary */
		ooy = 1.0f / (edgeptr->v2->y - edgeptr->v1->y);
		edgeptr->dxdy = (edgeptr->v2->x - edgeptr->v1->x) * ooy;
		for (paramnum = 0; paramnum < paramcount; paramnum++)
			edgeptr->dpdy[paramnum] = (edgeptr->v2->p[paramnum] - edgeptr->v1->p[paramnum]) * ooy;
		edgeptr++;
	}

	/* determine which list is left/right: */
	/* if the first vertex is shared, compare the slopes */
	/* if the first vertex is not shared, compare the X coordinates */
	if ((fedgelist[0].v1 == bedgelist[0].v1 && fedgelist[0].dxdy < bedgelist[0].dxdy) ||
		(fedgelist[0].v1 != bedgelist[0].v1 && fedgelist[0].v1->x < bedgelist[0].v1->x))
	{
		ledge = fedgelist;
		redge = bedgelist;
	}
	else
	{
		ledge = bedgelist;
		redge = fedgelist;
	}

	/* compute the X extents for each scanline */
	startunit = poly->unit_next;
	for (curscan = minyclip; curscan < maxyclip; curscan += scaninc)
	{
		uint32_t bucketnum = ((uint32_t)curscan / SCANLINES_PER_BUCKET) % TOTAL_BUCKETS;
		uint32_t unit_index = poly->unit_next++;
		quad_work_unit *unit = &poly->unit[unit_index]->quad;
		int extnum;

		/* determine how much to advance to hit the next bucket */
		scaninc = SCANLINES_PER_BUCKET - (uint32_t)curscan % SCANLINES_PER_BUCKET;

		/* fill in the work unit basics */
		unit->shared.polygon = polygon;
		unit->shared.count_next = std::min(maxyclip - curscan, scaninc);
		unit->shared.scanline = curscan;
		unit->shared.previtem = poly->unit_bucket[bucketnum];
		poly->unit_bucket[bucketnum] = unit_index;

		/* iterate over extents */
		for (extnum = 0; extnum < unit->shared.count_next; extnum++)
		{
			float fully = (float)(curscan + extnum) + 0.5f;
			float startx, stopx;
			int32_t istartx, istopx;
			int paramnum;

			/* compute the ending X based on which part of the triangle we're in */
			while (fully > ledge->v2->y && fully < v[maxv].y)
				ledge++;
			while (fully > redge->v2->y && fully < v[maxv].y)
				redge++;
			startx = ledge->v1->x + (fully - ledge->v1->y) * ledge->dxdy;
			stopx = redge->v1->x + (fully - redge->v1->y) * redge->dxdy;

			/* clamp to full pixels */
			istartx = round_coordinate(startx);
			istopx = round_coordinate(stopx);

			/* compute parameter starting points and deltas */
			if (paramcount > 0)
			{
				float ldy = fully - ledge->v1->y;
				float rdy = fully - redge->v1->y;
				float oox = 1.0f / (stopx - startx);

				/* iterate over parameters */
				for (paramnum = 0; paramnum < paramcount; paramnum++)
				{
					float lparam = ledge->v1->p[paramnum] + ldy * ledge->dpdy[paramnum];
					float rparam = redge->v1->p[paramnum] + rdy * redge->dpdy[paramnum];
					float dpdx = (rparam - lparam) * oox;

					unit->extent[extnum].param[paramnum].start = lparam;// - ((float)istartx + 0.5f) * dpdx;
					unit->extent[extnum].param[paramnum].dpdx = dpdx;
				}
			}

			/* include the right edge if requested */
			if (poly->flags & POLYLGCY_FLAG_INCLUDE_RIGHT_EDGE)
				istopx++;

			/* apply left/right clipping */
			if (istartx < cliprect.min_x)
			{
				for (paramnum = 0; paramnum < paramcount; paramnum++)
					unit->extent[extnum].param[paramnum].start += (cliprect.min_x - istartx) * unit->extent[extnum].param[paramnum].dpdx;
				istartx = cliprect.min_x;
			}
			if (istopx > cliprect.max_x)
				istopx = cliprect.max_x + 1;

			/* set the extent and update the total pixel count */
			if (istartx >= istopx)
				istartx = istopx = 0;
			unit->extent[extnum].startx = istartx;
			unit->extent[extnum].stopx = istopx;
			pixels += istopx - istartx;
		}
	}
#if KEEP_STATISTICS
	poly->unit_max = std::max(poly->unit_max, poly->unit_next);
#endif

	/* enqueue the work items */
	if (poly->queue != nullptr)
		osd_work_item_queue_multiple(poly->queue, poly_item_callback, poly->unit_next - startunit, poly->unit[startunit], poly->unit_size, WORK_ITEM_FLAG_AUTO_RELEASE);

	/* return the total number of pixels in the triangle */
	poly->quads++;
	poly->pixels += pixels;
	return pixels;
}



/***************************************************************************
    CLIPPING
***************************************************************************/

/*-------------------------------------------------
    poly_zclip_if_less - z clip a polygon against
    the given value, returning a set of clipped
    vertices
-------------------------------------------------*/

int poly_zclip_if_less(int numverts, const poly_vertex *v, poly_vertex *outv, int paramcount, float clipval)
{
	int prevclipped = (v[numverts - 1].p[0] < clipval);
	poly_vertex *nextout = outv;
	int vertnum;

	/* iterate over vertices */
	for (vertnum = 0; vertnum < numverts; vertnum++)
	{
		int thisclipped = (v[vertnum].p[0] < clipval);

		/* if we switched from clipped to non-clipped, interpolate a vertex */
		if (thisclipped != prevclipped)
			interpolate_vertex(nextout++, &v[(vertnum == 0) ? (numverts - 1) : (vertnum - 1)], &v[vertnum], paramcount, clipval);

		/* if this vertex is not clipped, copy it in */
		if (!thisclipped)
			copy_vertex(nextout++, &v[vertnum], paramcount);

		/* remember the last state */
		prevclipped = thisclipped;
	}
	return nextout - outv;
}



/***************************************************************************
    INTERNAL FUNCTIONS
***************************************************************************/

/*-------------------------------------------------
    poly_item_callback - callback for each poly
    item
-------------------------------------------------*/

static void *poly_item_callback(void *param, int threadid)
{
	while (1)
	{
		work_unit *unit = (work_unit *)param;
		polygon_info *polygon = unit->shared.polygon;
		int count = unit->shared.count_next & 0xffff;
		uint32_t orig_count_next;
		int curscan;

		/* if our previous item isn't done yet, enqueue this item to the end and proceed */
		if (unit->shared.previtem != 0xffff)
		{
			work_unit *prevunit = polygon->poly->unit[unit->shared.previtem];
			if (prevunit->shared.count_next != 0)
			{
				uint32_t unitnum = ((uint8_t *)unit - (uint8_t *)polygon->poly->unit[0]) / polygon->poly->unit_size;
				uint32_t new_count_next;

				/* attempt to atomically swap in this new value */
				do
				{
					orig_count_next = prevunit->shared.count_next;
					new_count_next = orig_count_next | (unitnum << 16);
				} while (!prevunit->shared.count_next.compare_exchange_weak(orig_count_next, new_count_next, std::memory_order_release, std::memory_order_relaxed));

#if KEEP_STATISTICS
				/* track resolved conflicts */
				polygon->poly->conflicts[threadid]++;
				if (orig_count_next != 0)
					polygon->poly->resolved[threadid]++;
#endif
				/* if we succeeded, skip out early so we can do other work */
				if (orig_count_next != 0)
					break;
			}
		}

		/* iterate over extents */
		for (curscan = 0; curscan < count; curscan++)
		{
			if (polygon->numverts == 3)
			{
				poly_extent tmpextent;
				convert_tri_extent_to_poly_extent(&tmpextent, &unit->tri.extent[curscan], polygon, unit->shared.scanline + curscan);
				(*polygon->callback)(polygon->dest, unit->shared.scanline + curscan, &tmpextent, polygon->extra, threadid);
			}
			else
				(*polygon->callback)(polygon->dest, unit->shared.scanline + curscan, &unit->quad.extent[curscan], polygon->extra, threadid);
		}

		/* set our count to 0 and re-fetch the original count value */
		do
		{
			orig_count_next = unit->shared.count_next;
		} while (!unit->shared.count_next.compare_exchange_weak(orig_count_next, 0, std::memory_order_release, std::memory_order_relaxed));

		/* if we have no more work to do, do nothing */
		orig_count_next >>= 16;
		if (orig_count_next == 0)
			break;
		param = polygon->poly->unit[orig_count_next];
	}
	return nullptr;
}


/*-------------------------------------------------
    poly_state_presave - pre-save callback to
    ensure everything is synced before saving
-------------------------------------------------*/

static void poly_state_presave(legacy_poly_manager &poly)
{
	poly_wait(&poly, "pre-save");
}
